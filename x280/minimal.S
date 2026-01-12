# Minimal X280 test - just write a value to a known address
# No stack, no C runtime, just pure assembly
# Uses PC-relative addressing so it works regardless of load address

.section .start, "ax"
.global _start
.type _start, @function

_start:
    # Compute mailbox address as: _start + 0x1000
    # Use auipc to get current PC, then adjust
    auipc   t0, 0           # t0 = PC of this instruction
    # t0 now points to _start, add 0x1000 to get mailbox
    li      t3, 0x1000
    add     t0, t0, t3      # t0 = mailbox address
    
    # Write a simple magic value first (0xDEADBEEF)
    li      t1, 0xDEADBEEF
    sw      t1, 0(t0)       # Store word at mailbox+0
    fence   w, w
    
    # Write to heartbeat (offset 8) 
    li      t1, 0x12345678
    sw      t1, 8(t0)
    fence   w, w
    
    # Write hart ID to offset 0x20
    csrr    t1, mhartid
    sd      t1, 0x20(t0)
    fence   w, w
    
    # Now write the "full" magic as 64-bit: 0x5846696E694C4548 ("HeLiFinX")
    li      t1, 0x5846696E      # Upper 32 bits
    slli    t1, t1, 32
    li      t2, 0x694C4548      # Lower 32 bits
    or      t1, t1, t2
    sd      t1, 0(t0)           # Store the full magic
    fence   w, w

    # Increment heartbeat forever
loop:
    lw      t1, 8(t0)           # Load heartbeat
    addi    t1, t1, 1           # Increment
    sw      t1, 8(t0)           # Store
    
    # Small delay
    li      t2, 10000
delay:
    addi    t2, t2, -1
    bnez    t2, delay
    
    j       loop

.size _start, . - _start
