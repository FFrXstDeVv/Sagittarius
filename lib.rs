#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

#[cfg(test)]
use bootloader::{entry_point, BootInfo};

#[cfg(test)]
entry_point!(test_kernel_main);

#[macro_use]
mod vga_buffer;
pub mod interrupts;
pub mod gdt;

pub mod memory;

pub fn init() {
    gdt::init();
    interrupts::init_idt();
    unsafe { interrupts::PICS.lock().initialize() };
    x86_64::instructions::interrupts::enable();     // new
}

pub fn hlt_loop() -> ! {
    loop {
        x86_64::instructions::hlt();
    }
}