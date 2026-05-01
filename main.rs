#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

mod vga_buffer;
use sagittarius_os::interrupts;
mod gdt;

use core::panic::PanicInfo;

use bootloader::{BootInfo, entry_point};

entry_point!(kernel_main);

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    println!("{}", info);
    println!("Run stopped.");
    loop {}
}

fn kernel_main(boot_info: &'static BootInfo) -> ! {
    use sagittarius_os::memory::active_level_4_table;
    use sagittarius_os::memory::translate_addr;
    use x86_64::VirtAddr;

    println!("Hello World{}", "!");
    sagittarius_os::init();
    println!("Sagittarius OS initialised");

    let phys_mem_offset = VirtAddr::new(boot_info.physical_memory_offset);
    let l4_table = unsafe { active_level_4_table(phys_mem_offset) };

    let addresses = [
        // the identity-mapped vga buffer page
        0xb8000,
        // some code page
        0x201008,
        // some stack page
        0x0100_0020_1a10,
        // virtual address mapped to physical address 0
        boot_info.physical_memory_offset,
    ];

    for &address in &addresses {
        let virt = VirtAddr::new(address);
        let phys = unsafe { translate_addr(virt, phys_mem_offset) };
        println!("{:?} -> {:?}", virt, phys);
    }


    use x86_64::structures::paging::PageTable;
    for (i, entry) in l4_table.iter().enumerate() {
        if !entry.is_unused() {
            println!("L4 Entry {}: {:?}", i, entry);

            // get the physical address from the entry and convert it
            let phys = entry.frame().unwrap().start_address();
            let virt = phys.as_u64() + boot_info.physical_memory_offset;
            let ptr = VirtAddr::new(virt).as_mut_ptr();
            let l3_table: &PageTable = unsafe { &*ptr };

            // print non-empty entries of the level 3 table
            for (i, entry) in l3_table.iter().enumerate() {
                if !entry.is_unused() {
                    println!("  L3 Entry {}: {:?}", i, entry);
                }
            }
        }
    }

    // as before
    #[cfg(test)]
    test_main();

    println!("It did not crash!");
    sagittarius_os::hlt_loop();
}