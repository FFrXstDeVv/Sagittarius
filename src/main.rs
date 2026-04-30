#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

mod vga_buffer;
mod interrupts;
mod gdt;

use core::panic::PanicInfo;

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    println!("{}", info);
    println!("Run stopped.");
    loop {}
}

#[unsafe(no_mangle)]
pub extern "C" fn _start() -> ! {
    gdt::init();
    interrupts::init_idt();

    fn stack_overflow() {
        stack_overflow();
    }

    stack_overflow();

    println!("It did not crash!");
    loop {}
}