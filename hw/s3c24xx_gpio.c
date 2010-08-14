/* hw/s3c24xx_gpio.c
 *
 * Samsung S3C24XX GPIO emulation (mostly for E-INT)
 *
 * Copyright 2006, 2007 Daniel Silverstone and Vincent Sanders
 *
 * This file is under the terms of the GNU General Public
 * License Version 2
 */

#include "hw.h"
#include "s3c24xx.h"

#define S3C_GPIO_GPECON (0x40)
#define S3C_GPIO_GPEDAT (0x44)
#define S3C_GPIO_GPEUP (0x48)

#define S3C_GPIO_EINT_MASK (0xA4)
#define S3C_GPIO_EINT_PEND (0xA8)
#define S3C_GPIO_GSTATUS0 (0xAC)
#define S3C_GPIO_GSTATUS1 (0xB0)
#define S3C_GPIO_GSTATUS2 (0xB4)
#define S3C_GPIO_GSTATUS3 (0xB8)
#define S3C_GPIO_GSTATUS4 (0xBC)


#define GPRN(r) (r>>2)
#define GPR(P) s->gpio_reg[P>>2]

/* GPIO controller state */
struct s3c24xx_gpio_state_s {
    uint32_t gpio_reg[47];

    qemu_irq *eirqs; /* gpio external interrupts */

    qemu_irq irqs[6]; /* cpu irqs to cascade */
};

static void
s3c24xx_gpio_propogate_eint(struct s3c24xx_gpio_state_s *s)
{
    uint32_t ints, i;

    ints = GPR(S3C_GPIO_EINT_PEND) & ~GPR(S3C_GPIO_EINT_MASK);

    /* EINT0 - EINT3 are INT0 - INT3 */
    for (i=0; i < 4; ++i)
        qemu_set_irq(s->irqs[i], (ints & (1<<i))?1:0);

    /* EINT4 - EINT7 are INT4 */
    qemu_set_irq(s->irqs[4], (ints & 0xf0)?1:0);

    /* EINT8 - EINT23 are INT5 */
    qemu_set_irq(s->irqs[5], (ints & 0x00ffff00)?1:0);
}

static uint32_t
gpio_con_to_mask(uint32_t con)
{
    uint32_t mask = 0x0;
    int bit;

    for (bit = 0; bit < 16; bit++) {
        if (((con >> (bit*2)) & 0x3) == 0x01)
            mask |= 1 << bit;
    }

    return mask;
}

static void
s3c24xx_gpio_write_f(void *opaque, target_phys_addr_t addr_, uint32_t value)
{
    struct s3c24xx_gpio_state_s *s = (struct s3c24xx_gpio_state_s *)opaque;
    int addr = (addr_ >> 2) & 0x3f;

    if (addr < 0 || addr > 47)
        addr = 47;

    if (addr == (S3C_GPIO_EINT_MASK>>2))
        value &= ~0xf; /* cannot mask EINT0-EINT3 */

    if (addr == (S3C_GPIO_EINT_PEND>>2)) {
        s->gpio_reg[addr] &= ~value;
    } else {
        if (addr < (0x80/4) && (addr_ & 0xf) == 0x04) {
            uint32_t mask = gpio_con_to_mask(s->gpio_reg[addr - 1]);

            value &= mask;

            s->gpio_reg[addr] &= ~mask;
            s->gpio_reg[addr] |= value;
        } else
            s->gpio_reg[addr] = value;
    }

    if ((addr == (S3C_GPIO_EINT_MASK)>>2) ||
        (addr == (S3C_GPIO_EINT_PEND)>>2)) {
        /* A write to the EINT regs leads us to determine the interrupts to
         * propagate
         */
        s3c24xx_gpio_propogate_eint(s);
    }
}

static uint32_t
s3c24xx_gpio_read_f(void *opaque, target_phys_addr_t addr_)
{
    struct s3c24xx_gpio_state_s *s = (struct s3c24xx_gpio_state_s *)opaque;
    uint32_t addr = (addr_ >> 2);
    uint32_t ret;

    if (addr > GPRN(S3C_GPIO_GSTATUS4))
        addr = GPRN(S3C_GPIO_GSTATUS4);

    ret = s->gpio_reg[addr];

    if (addr == GPRN(S3C_GPIO_GPEDAT)) {
        /* IIC pins are special function pins on GPE14 and GPE15. If GPE is is
         * in input mode make the IIC lines appear to be pulled high. This is
         * neccissary because OS i2c drivers use this to ensure the I2C bus is
         * clear.
         */
        if ((GPR(S3C_GPIO_GPECON) & (3<<28)) == 0)
            ret |= 1 << 14;

        if ((GPR(S3C_GPIO_GPECON) & (3<<30)) == 0)
            ret |= 1 << 15;
    }

    return ret;
}


static CPUReadMemoryFunc * const s3c24xx_gpio_read[] = {
    s3c24xx_gpio_read_f,
    s3c24xx_gpio_read_f,
    s3c24xx_gpio_read_f
};

static CPUWriteMemoryFunc * const s3c24xx_gpio_write[] = {
    s3c24xx_gpio_write_f,
    s3c24xx_gpio_write_f,
    s3c24xx_gpio_write_f
};

static void
s3c24xx_gpio_irq_handler(void *opaque, int n, int level)
{
    struct s3c24xx_gpio_state_s *s = (struct s3c24xx_gpio_state_s *)opaque;

    if (level)
        GPR(S3C_GPIO_EINT_PEND) |= (1<<n);

    s3c24xx_gpio_propogate_eint(s);
}

static void s3c24xx_gpio_save(QEMUFile *f, void *opaque)
{
    struct s3c24xx_gpio_state_s *s = (struct s3c24xx_gpio_state_s *)opaque;
    int i;

    for (i = 0; i < 47; i ++)
        qemu_put_be32s(f, &s->gpio_reg[i]);
}

static int s3c24xx_gpio_load(QEMUFile *f, void *opaque, int version_id)
{
    struct s3c24xx_gpio_state_s *s = (struct s3c24xx_gpio_state_s *)opaque;
    int i;

    for (i = 0; i < 47; i ++)
        qemu_get_be32s(f, &s->gpio_reg[i]);

    return 0;
}

struct s3c24xx_gpio_state_s *
s3c24xx_gpio_init(S3CState *soc, target_phys_addr_t base_addr, uint32_t cpu_id)
{
    /* Samsung S3C24XX GPIO
     *
     * The primary operation here is the ID register and IRQs
     */
    struct s3c24xx_gpio_state_s *s;
    int tag;
    int i;

    s = qemu_mallocz(sizeof(struct s3c24xx_gpio_state_s));
    if (!s)
        return NULL;

    tag = cpu_register_io_memory(s3c24xx_gpio_read, s3c24xx_gpio_write, s);
    cpu_register_physical_memory(base_addr, 47 * 4, tag);
    register_savevm(NULL, "s3c24xx_gpio", 0, 0, s3c24xx_gpio_save, s3c24xx_gpio_load, s);

    /* set non zero default values */
    GPR(0x00) = 0x7fffff;
    GPR(0x34) = 0xfefc;
    GPR(0x38) = 0xf000;
    GPR(0x68) = 0xf800;
    GPR(0x80) = 0x10330;
    GPR(0xA4) = 0xfffff0;
    GPR(S3C_GPIO_GSTATUS1) = cpu_id;
    GPR(S3C_GPIO_GSTATUS2) = 1;
    GPR(S3C_GPIO_GSTATUS3) = 0;
    GPR(S3C_GPIO_GSTATUS4) = 0;

    /* obtain first level IRQs for cascade */
    for (i = 0; i <= 5; i++) {
        s->irqs[i] = s3c24xx_get_irq(soc->irq, i);
    }

    /* EINTs 0-23 -- Only 24, not 48 because EINTs are not level */
    s->eirqs = qemu_allocate_irqs(s3c24xx_gpio_irq_handler, s, 24);

    return s;
}

/* get the qemu interrupt from an eirq number */
qemu_irq
s3c24xx_get_eirq(struct s3c24xx_gpio_state_s *s, int einum)
{
    return s->eirqs[einum];
}
