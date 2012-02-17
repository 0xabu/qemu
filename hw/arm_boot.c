/*
 * ARM kernel loader.
 *
 * Copyright (c) 2006-2007 CodeSourcery.
 * Written by Paul Brook
 *
 * This code is licensed under the GPL.
 */

#include "hw.h"
#include "arm-misc.h"
#include "sysemu.h"
#include "loader.h"
#include "elf.h"

#define KERNEL_ARGS_ADDR 0x100
#define KERNEL_LOAD_ADDR 0x00010000
#define INITRD_LOAD_ADDR 0x00d00000

/* The worlds second smallest bootloader.  Set r0-r2, then jump to kernel.  */
static uint32_t bootloader[] = {
  0xe3a00000, /* mov     r0, #0 */
  0xe59f1004, /* ldr     r1, [pc, #4] */
  0xe59f2004, /* ldr     r2, [pc, #4] */
  0xe59ff004, /* ldr     pc, [pc, #4] */
  0, /* Board ID */
  0, /* Address of kernel args. */
  0  /* Kernel entry point. */
};

/* Handling for secondary CPU boot in a multicore system.
 * Unlike the uniprocessor/primary CPU boot, this is platform
 * dependent. The default code here is based on the secondary
 * CPU boot protocol used on realview/vexpress boards, with
 * some parameterisation to increase its flexibility.
 * QEMU platform models for which this code is not appropriate
 * should override write_secondary_boot and secondary_cpu_reset_hook
 * instead.
 *
 * This code enables the interrupt controllers for the secondary
 * CPUs and then puts all the secondary CPUs into a loop waiting
 * for an interprocessor interrupt and polling a configurable
 * location for the kernel secondary CPU entry point.
 */
static uint32_t smpboot[] = {
  0xe59f201c, /* ldr r2, gic_cpu_if */
  0xe59f001c, /* ldr r0, startaddr */
  0xe3a01001, /* mov r1, #1 */
  0xe5821000, /* str r1, [r2] */
  0xe320f003, /* wfi */
  0xe5901000, /* ldr     r1, [r0] */
  0xe1110001, /* tst     r1, r1 */
  0x0afffffb, /* beq     <wfi> */
  0xe12fff11, /* bx      r1 */
  0,          /* gic_cpu_if: base address of GIC CPU interface */
  0           /* bootreg: Boot register address is held here */
};

static void default_write_secondary(CPUState *env,
                                    const struct arm_boot_info *info)
{
    int n;
    smpboot[ARRAY_SIZE(smpboot) - 1] = info->smp_bootreg_addr;
    smpboot[ARRAY_SIZE(smpboot) - 2] = info->gic_cpu_if_addr;
    for (n = 0; n < ARRAY_SIZE(smpboot); n++) {
        smpboot[n] = tswap32(smpboot[n]);
    }
    rom_add_blob_fixed("smpboot", smpboot, sizeof(smpboot),
                       info->smp_loader_start);
}

static void default_reset_secondary(CPUState *env,
                                    const struct arm_boot_info *info)
{
    stl_phys_notdirty(info->smp_bootreg_addr, 0);
    env->regs[15] = info->smp_loader_start;
}

#define WRITE_WORD(p, value) do { \
    stl_phys_notdirty(p, value);  \
    p += 4;                       \
} while (0)

static void set_kernel_args(const struct arm_boot_info *info)
{
    int initrd_size = info->initrd_size;
    target_phys_addr_t base = info->loader_start;
    target_phys_addr_t p;

    p = info->loader_start + KERNEL_ARGS_ADDR;
    /* ATAG_CORE */
    WRITE_WORD(p, 5);
    WRITE_WORD(p, 0x54410001);
    WRITE_WORD(p, 1);
    WRITE_WORD(p, 0x1000);
    WRITE_WORD(p, 0);
    /* ATAG_MEM */
    /* TODO: handle multiple chips on one ATAG list */
    WRITE_WORD(p, 4);
    WRITE_WORD(p, 0x54410002);
    WRITE_WORD(p, info->ram_size);
    WRITE_WORD(p, info->loader_start);
    if (info->initrd_size) {
        /* ATAG_INITRD2 */
        WRITE_WORD(p, 4);
        WRITE_WORD(p, 0x54420005);
        WRITE_WORD(p, info->loader_start + INITRD_LOAD_ADDR);
        WRITE_WORD(p, info->initrd_size);
    }
    if (info->atag_revision) {
        /* ATAG REVISION. */
        WRITE_WORD(p, 3);
        WRITE_WORD(p, 0x54410007);
        WRITE_WORD(p, info->atag_revision);
    }
    if (info->kernel_cmdline && *info->kernel_cmdline) {
        /* ATAG_CMDLINE */
        int cmdline_size;

        cmdline_size = strlen(info->kernel_cmdline);
        cpu_physical_memory_write(p + 8, (void *)info->kernel_cmdline,
                                  cmdline_size + 1);
        cmdline_size = (cmdline_size >> 2) + 1;
        WRITE_WORD(p, cmdline_size + 2);
        WRITE_WORD(p, 0x54410009);
        p += cmdline_size * 4;
    }
    if (info->atag_board) {
        /* ATAG_BOARD */
        int atag_board_len;
        uint8_t atag_board_buf[0x1000];

        atag_board_len = (info->atag_board(info, atag_board_buf) + 3) & ~3;
        WRITE_WORD(p, (atag_board_len + 8) >> 2);
        WRITE_WORD(p, 0x414f4d50);
        cpu_physical_memory_write(p, atag_board_buf, atag_board_len);
        p += atag_board_len;
    }
    /* ATAG_END */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
}

static void set_kernel_args_old(const struct arm_boot_info *info)
{
    target_phys_addr_t p;
    const char *s;
    int initrd_size = info->initrd_size;
    target_phys_addr_t base = info->loader_start;

    /* see linux/include/asm-arm/setup.h */
    p = info->loader_start + KERNEL_ARGS_ADDR;
    /* page_size */
    WRITE_WORD(p, 4096);
    /* nr_pages */
    WRITE_WORD(p, info->ram_size / 4096);
    /* ramdisk_size */
    WRITE_WORD(p, 0);
#define FLAG_READONLY	1
#define FLAG_RDLOAD	4
#define FLAG_RDPROMPT	8
    /* flags */
    WRITE_WORD(p, FLAG_READONLY | FLAG_RDLOAD | FLAG_RDPROMPT);
    /* rootdev */
    WRITE_WORD(p, (31 << 8) | 0);	/* /dev/mtdblock0 */
    /* video_num_cols */
    WRITE_WORD(p, 0);
    /* video_num_rows */
    WRITE_WORD(p, 0);
    /* video_x */
    WRITE_WORD(p, 0);
    /* video_y */
    WRITE_WORD(p, 0);
    /* memc_control_reg */
    WRITE_WORD(p, 0);
    /* unsigned char sounddefault */
    /* unsigned char adfsdrives */
    /* unsigned char bytes_per_char_h */
    /* unsigned char bytes_per_char_v */
    WRITE_WORD(p, 0);
    /* pages_in_bank[4] */
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    WRITE_WORD(p, 0);
    /* pages_in_vram */
    WRITE_WORD(p, 0);
    /* initrd_start */
    if (info->initrd_size)
        WRITE_WORD(p, info->loader_start + INITRD_LOAD_ADDR);
    else
        WRITE_WORD(p, 0);
    /* initrd_size */
    WRITE_WORD(p, info->initrd_size);
    /* rd_start */
    WRITE_WORD(p, 0);
    /* system_rev */
    WRITE_WORD(p, 0);
    /* system_serial_low */
    WRITE_WORD(p, 0);
    /* system_serial_high */
    WRITE_WORD(p, 0);
    /* mem_fclk_21285 */
    WRITE_WORD(p, 0);
    /* zero unused fields */
    while (p < info->loader_start + KERNEL_ARGS_ADDR + 256 + 1024) {
        WRITE_WORD(p, 0);
    }
    s = info->kernel_cmdline;
    if (s) {
        cpu_physical_memory_write(p, (void *)s, strlen(s) + 1);
    } else {
        WRITE_WORD(p, 0);
    }
}

static void do_cpu_reset(void *opaque)
{
    CPUState *env = opaque;
    const struct arm_boot_info *info = env->boot_info;

    cpu_reset(env);
    if (info) {
        if (!info->is_linux) {
            /* Jump to the entry point.  */
            env->regs[15] = info->entry & 0xfffffffe;
            env->thumb = info->entry & 1;
        } else {
            if (env == first_cpu) {
                env->regs[15] = info->loader_start;
                if (old_param) {
                    set_kernel_args_old(info);
                } else {
                    set_kernel_args(info);
                }
            } else {
                info->secondary_cpu_reset_hook(env, info);
            }
        }
    }
}

void arm_load_kernel(CPUState *env, struct arm_boot_info *info)
{
    int kernel_size;
    int initrd_size;
    int n;
    int is_linux;
    uint64_t elf_entry;
    target_phys_addr_t entry;

    /* Load the kernel.  */
    if (!info->kernel_filename) {
        fprintf(stderr, "Kernel image must be specified\n");
        exit(1);
    }

    if (!info->secondary_cpu_reset_hook) {
        info->secondary_cpu_reset_hook = default_reset_secondary;
    }
    if (!info->write_secondary_boot) {
        info->write_secondary_boot = default_write_secondary;
    }

    if (info->nb_cpus == 0)
        info->nb_cpus = 1;

    /* Assume that raw images are linux kernels, and ELF images are not.  */
    /* If the filename contains 'vmlinux', assume ELF images are linux, too. */
    is_linux = (strstr(info->kernel_filename, "vmlinux") != NULL);
    kernel_size = load_elf(info->kernel_filename, NULL, NULL, &elf_entry,
                           NULL, NULL, env->bigendian, ELF_MACHINE, 1);
    entry = elf_entry;
    if (kernel_size < 0) {
        kernel_size = load_uimage(info->kernel_filename, &entry, NULL,
                                  &is_linux);
    }
    if (kernel_size < 0) {
        entry = info->loader_start + KERNEL_LOAD_ADDR;
        kernel_size = load_image_targphys(info->kernel_filename, entry,
                                          ram_size - KERNEL_LOAD_ADDR);
        is_linux = 1;
    }
    if (kernel_size < 0) {
        fprintf(stderr, "qemu: could not load kernel '%s'\n",
                info->kernel_filename);
        exit(1);
    }
    info->entry = entry;
    if (is_linux) {
        if (info->initrd_filename) {
            initrd_size = load_image_targphys(info->initrd_filename,
                                              info->loader_start
                                              + INITRD_LOAD_ADDR,
                                              ram_size - INITRD_LOAD_ADDR);
            if (initrd_size < 0) {
                fprintf(stderr, "qemu: could not load initrd '%s'\n",
                        info->initrd_filename);
                exit(1);
            }
        } else {
            initrd_size = 0;
        }
        bootloader[4] = info->board_id;
        bootloader[5] = info->loader_start + KERNEL_ARGS_ADDR;
        bootloader[6] = entry;
        for (n = 0; n < sizeof(bootloader) / 4; n++) {
            bootloader[n] = tswap32(bootloader[n]);
        }
        rom_add_blob_fixed("bootloader", bootloader, sizeof(bootloader),
                           info->loader_start);
        if (info->nb_cpus > 1) {
            info->write_secondary_boot(env, info);
        }
        info->initrd_size = initrd_size;
    }
    info->is_linux = is_linux;

    for (; env; env = env->next_cpu) {
        env->boot_info = info;
        qemu_register_reset(do_cpu_reset, env);
    }
}
