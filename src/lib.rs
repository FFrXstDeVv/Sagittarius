#![no_std]
#![no_main]
#![feature(abi_x86_interrupt)]

#[macro_use]
mod vga_buffer;
pub mod interrupts;
pub mod gdt;

pub fn init() {
    gdt::init();
    interrupts::init_idt();
    unsafe { interrupts::PICS.lock().initialize() };
    x86_64::instructions::interrupts::enable();     // new
}