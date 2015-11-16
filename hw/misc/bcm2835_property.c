/*
 * Raspberry Pi emulation (c) 2012 Gregory Estrade
 * This code is licensed under the GNU GPLv2 and later.
 */

#include "hw/display/framebuffer.h"
#include "hw/sysbus.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "exec/address-spaces.h"

#include "hw/arm/bcm2835_common.h"

#define TYPE_BCM2835_PROPERTY "bcm2835_property"
#define BCM2835_PROPERTY(obj) \
        OBJECT_CHECK(Bcm2835PropertyState, (obj), TYPE_BCM2835_PROPERTY)

typedef struct {
    SysBusDevice busdev;
    MemoryRegion iomem;
    int pending;
    qemu_irq mbox_irq;

    uint32_t addr;
} Bcm2835PropertyState;

static void update_fb(void)
{
    bcm2835_fb.lock = 1;

    bcm2835_fb.base = bcm2835_vcram_base;
    bcm2835_fb.base += BCM2835_FB_OFFSET;

    /* TODO - Manage properly virtual resolution */

    bcm2835_fb.pitch = bcm2835_fb.xres * (bcm2835_fb.bpp >> 3);
    bcm2835_fb.size = bcm2835_fb.yres * bcm2835_fb.pitch;
}

/* https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface */

static void bcm2835_property_mbox_push(Bcm2835PropertyState *s,
    uint32_t value)
{
    uint32_t tag;
    uint32_t bufsize;
    uint32_t tot_len;
    int n;
    int resplen;
    uint32_t offset, length, color;
    uint32_t tmp;

    value &= ~0xf;

    /* XXX: According to the doc link above, the physical address
       should be used. However, the Windows UEFI loader is observed
       talking to us using the uncached mapping address (of
       0xc0000000), and evidently that is expected to work as well.
     */
    if (value > 0xc0000000) {
        value -= 0xc0000000;
    }

    s->addr = value;

    tot_len = ldl_phys(&address_space_memory, value);

    /* @(s->addr + 4) : Buffer response code */
    value = s->addr + 8;
    while (value + 8 <= s->addr + tot_len) {
        tag = ldl_phys(&address_space_memory, value);
        bufsize = ldl_phys(&address_space_memory, value + 4);
        /* @(value + 8) : Request/response indicator */
        resplen = 0;
        switch (tag) {
        case 0x00000000: /* End tag */
            break;
        case 0x00000001: /* Get firmware revision */
            stl_phys(&address_space_memory, value + 12, 346337);
            resplen = 4;
            break;

        case 0x00010001: /* Get board model */
            resplen = 4;
            break;
        case 0x00010002: /* Get board revision */
            resplen = 4;
            break;
        case 0x00010003: /* Get board MAC address */
            /* write the first four bytes of the 6-byte MAC */
            stl_phys(&address_space_memory, value + 12, 0xB827EBD0);
            /* write the last two bytes, avoid any write past the buffer end */
            stb_phys(&address_space_memory, value + 16, 0xEE);
            stb_phys(&address_space_memory, value + 17, 0xDF);
            resplen = 6;
            break;
        case 0x00010004: /* Get board serial */
            resplen = 8;
            break;
        case 0x00010005: /* Get ARM memory */
            /* base */
            stl_phys(&address_space_memory, value + 12, 0);
            /* size */
            stl_phys(&address_space_memory, value + 16, bcm2835_vcram_base);
            resplen = 8;
            break;
        case 0x00010006: /* Get VC memory */
            /* base */
            stl_phys(&address_space_memory, value + 12, bcm2835_vcram_base);
            /* size */
            stl_phys(&address_space_memory, value + 16, VCRAM_SIZE);
            resplen = 8;
            break;
        case 0x00028001: /* Set power state */
            /* Assume that whatever device they asked for exists,
             * and we'll just claim we set it to the desired state */
            tmp = ldl_phys(&address_space_memory, value + 16);
            stl_phys(&address_space_memory, value + 16, (tmp & 1));
            resplen = 8;
            break;

        /* Clocks */

        case 0x00030001: /* Get clock state */
            stl_phys(&address_space_memory, value + 16, 0x1);
            resplen = 8;
            break;

        case 0x00038001: /* Set clock state */
            resplen = 8;
            break;

        case 0x00030002: /* Get clock rate */
        case 0x00030004: /* Get max clock rate */
        case 0x00030007: /* Get min clock rate */
            switch (ldl_phys(&address_space_memory, value + 12)) {
            case 1: /* EMMC */
                stl_phys(&address_space_memory, value + 16, 50000000);
                break;
            case 2: /* UART */
                stl_phys(&address_space_memory, value + 16, 3000000);
                break;
            default:
                stl_phys(&address_space_memory, value + 16, 700000000);
                break;
            }
            resplen = 8;
            break;

        case 0x00038002: /* Set clock rate */
        case 0x00038004: /* Set max clock rate */
        case 0x00038007: /* Set min clock rate */
            resplen = 8;
            break;

        /* Temperature */

        case 0x00030006: /* Get temperature */
            stl_phys(&address_space_memory, value + 16, 25000);
            resplen = 8;
            break;

        case 0x0003000A: /* Get max temperature */
            stl_phys(&address_space_memory, value + 16, 99000);
            resplen = 8;
            break;


        /* Frame buffer */

        case 0x00040001: /* Allocate buffer */
            stl_phys(&address_space_memory, value + 12, bcm2835_fb.base);
            stl_phys(&address_space_memory, value + 16, bcm2835_fb.size);
            resplen = 8;
            break;
        case 0x00048001: /* Release buffer */
            resplen = 0;
            break;
        case 0x00040002: /* Blank screen */
            resplen = 4;
            break;
        case 0x00040003: /* Get display width/height */
        case 0x00040004:
            stl_phys(&address_space_memory, value + 12, bcm2835_fb.xres);
            stl_phys(&address_space_memory, value + 16, bcm2835_fb.yres);
            resplen = 8;
            break;
        case 0x00044003: /* Test display width/height */
        case 0x00044004:
            resplen = 8;
            break;
        case 0x00048003: /* Set display width/height */
        case 0x00048004:
            bcm2835_fb.xres = ldl_phys(&address_space_memory, value + 12);
            bcm2835_fb.yres = ldl_phys(&address_space_memory, value + 16);
            update_fb();
            resplen = 8;
            break;
        case 0x00040005: /* Get depth */
            stl_phys(&address_space_memory, value + 12, bcm2835_fb.bpp);
            resplen = 4;
            break;
        case 0x00044005: /* Test depth */
            resplen = 4;
            break;
        case 0x00048005: /* Set depth */
            bcm2835_fb.bpp = ldl_phys(&address_space_memory, value + 12);
            update_fb();
            resplen = 4;
            break;
        case 0x00040006: /* Get pixel order */
            stl_phys(&address_space_memory, value + 12, bcm2835_fb.pixo);
            resplen = 4;
            break;
        case 0x00044006: /* Test pixel order */
            resplen = 4;
            break;
        case 0x00048006: /* Set pixel order */
            bcm2835_fb.pixo = ldl_phys(&address_space_memory, value + 12);
            update_fb();
            resplen = 4;
            break;
        case 0x00040007: /* Get alpha */
            stl_phys(&address_space_memory, value + 12, bcm2835_fb.alpha);
            resplen = 4;
            break;
        case 0x00044007: /* Test pixel alpha */
            resplen = 4;
            break;
        case 0x00048007: /* Set alpha */
            bcm2835_fb.alpha = ldl_phys(&address_space_memory, value + 12);
            update_fb();
            resplen = 4;
            break;
        case 0x00040008: /* Get pitch */
            stl_phys(&address_space_memory, value + 12, bcm2835_fb.pitch);
            resplen = 4;
            break;
        case 0x00040009: /* Get virtual offset */
            stl_phys(&address_space_memory, value + 12, bcm2835_fb.xoffset);
            stl_phys(&address_space_memory, value + 16, bcm2835_fb.yoffset);
            resplen = 8;
            break;
        case 0x00044009: /* Test virtual offset */
            resplen = 8;
            break;
        case 0x00048009: /* Set virtual offset */
            bcm2835_fb.xoffset = ldl_phys(&address_space_memory, value + 12);
            bcm2835_fb.yoffset = ldl_phys(&address_space_memory, value + 16);
            update_fb();
            stl_phys(&address_space_memory, value + 12, bcm2835_fb.xres);
            stl_phys(&address_space_memory, value + 16, bcm2835_fb.yres);
            resplen = 8;
            break;
        case 0x0004000a: /* Get/Test/Set overscan */
        case 0x0004400a:
        case 0x0004800a:
            stl_phys(&address_space_memory, value + 12, 0);
            stl_phys(&address_space_memory, value + 16, 0);
            stl_phys(&address_space_memory, value + 20, 0);
            stl_phys(&address_space_memory, value + 24, 0);
            resplen = 16;
            break;

        case 0x0004800b: /* Set palette */
            offset = ldl_phys(&address_space_memory, value + 12);
            length = ldl_phys(&address_space_memory, value + 16);
            n = 0;
            while (n < length - offset) {
                color = ldl_phys(&address_space_memory, value + 20 + (n << 2));
                stl_phys(&address_space_memory,
                         bcm2835_vcram_base + ((offset + n) << 2), color);
                n++;
            }
            stl_phys(&address_space_memory, value + 12, 0);
            resplen = 4;
            break;

        case 0x00060001: /* Get DMA channels */
            /* channels 2-5 */
            stl_phys(&address_space_memory, value + 12, 0x003C);
            resplen = 4;
            break;

        case 0x00050001: /* Get command line */
            resplen = 0;
            break;

        default:
            qemu_log_mask(LOG_GUEST_ERROR,
                "bcm2835_property: unhandled tag %08x\n", tag);
            break;
        }

        if (tag == 0) {
            break;
        }

        stl_phys(&address_space_memory, value + 8, (1 << 31) | resplen);
        value += bufsize + 12;
    }

    /* Buffer response code */
    stl_phys(&address_space_memory, s->addr + 4, (1 << 31));

    if (bcm2835_fb.lock) {
        bcm2835_fb.invalidate = 1;
        qemu_console_resize(bcm2835_fb.con, bcm2835_fb.xres, bcm2835_fb.yres);
        bcm2835_fb.lock = 0;
    }
}

static uint64_t bcm2835_property_read(void *opaque, hwaddr offset,
    unsigned size)
{
    Bcm2835PropertyState *s = (Bcm2835PropertyState *)opaque;
    uint32_t res = 0;

    switch (offset) {
    case 0:
        res = MBOX_CHAN_PROPERTY | s->addr;
        s->pending = 0;
        qemu_set_irq(s->mbox_irq, 0);
        break;
    case 4:
        res = s->pending;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_property_read: Bad offset %x\n", (int)offset);
        return 0;
    }
    return res;
}
static void bcm2835_property_write(void *opaque, hwaddr offset,
    uint64_t value, unsigned size)
{
    Bcm2835PropertyState *s = (Bcm2835PropertyState *)opaque;
    switch (offset) {
    case 0:
        if (!s->pending) {
            s->pending = 1;
            bcm2835_property_mbox_push(s, value);
            qemu_set_irq(s->mbox_irq, 1);
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "bcm2835_property_write: Bad offset %x\n", (int)offset);
        return;
    }

}


static const MemoryRegionOps bcm2835_property_ops = {
    .read = bcm2835_property_read,
    .write = bcm2835_property_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};


static const VMStateDescription vmstate_bcm2835_property = {
    .name = TYPE_BCM2835_PROPERTY,
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_END_OF_LIST()
    }
};

static int bcm2835_property_init(SysBusDevice *sbd)
{
    DeviceState *dev = DEVICE(sbd);
    Bcm2835PropertyState *s = BCM2835_PROPERTY(dev);

    s->pending = 0;
    s->addr = 0;

    sysbus_init_irq(sbd, &s->mbox_irq);
    memory_region_init_io(&s->iomem, OBJECT(s), &bcm2835_property_ops, s,
        TYPE_BCM2835_PROPERTY, 0x10);
    sysbus_init_mmio(sbd, &s->iomem);
    vmstate_register(dev, -1, &vmstate_bcm2835_property, s);

    return 0;
}

static void bcm2835_property_class_init(ObjectClass *klass, void *data)
{
    SysBusDeviceClass *sdc = SYS_BUS_DEVICE_CLASS(klass);

    sdc->init = bcm2835_property_init;
}

static TypeInfo bcm2835_property_info = {
    .name          = TYPE_BCM2835_PROPERTY,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Bcm2835PropertyState),
    .class_init    = bcm2835_property_class_init,
};

static void bcm2835_property_register_types(void)
{
    type_register_static(&bcm2835_property_info);
}

type_init(bcm2835_property_register_types)
