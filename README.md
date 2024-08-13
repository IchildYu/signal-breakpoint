# signal-breakpoint

I don't really know how to call this...

When I was using `load-elf` with arm/arm64, I found it somehow complex to debug with gdb. I just want to load elf and see some output at the middle of any function in the library, but I need to run qemu with debug and need another terminal to open gdb and even write gdb script. Why not just write c code and run with qemu? This would be much simpler (however, gdb python script may be simpler than c code).

So, ~~just implant gdb to my c code~~ write a simple breakpoint and handler implementation.

Finally, I decided to implement it using signal, or SIGTRAP, more precisely. Then,,, if you use this, debugging your new code with gdb would be a nightmare ...... However, if you need gdb, you can just ignore this library and write gdb script; And using this means you don't (or can't?) need gdb,,,,,, as long as you do not need gdb to debug your new code,,,,,, aaaaaa,,,,,,, eeeeeeeee,,,,,,,, then good luck!

Back to main topic, set breakpoint is easy, just write a debugbreak instruction. When program runs to here, it triggers a SIGTRAP signal, enters our prepared handler function and dispatches ctx to your specified handler for the address that triggered signal.

But going back to normal execution is somehow difficult. Obviously we should replace the debugbreak instruction with original instruction, but how to put debugbreak instruction back after this instruction being executed?

For x86/x64, there's a TRAP flag in eflags. When set, every instruction would trigger a SIGTRAP (like single step in debugger). So we can set TRAP flag after our breakpoint handler being executed. When next instruction triggered SIGTRAP, we can put debugbreak instruction back and clear TRAP flag, and everything would be OK.

But for arm/arm64, I didn't find any "TRAP flag" (maybe there is?), and I just create another breakpoint at the next instruction. Looks like it works, but there're still bugs... If that breakpoint is a branch instruction,,,, then good luck again!
