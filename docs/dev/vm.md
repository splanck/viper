# VM Internals

## Block Parameter Slots

On control transfer the interpreter evaluates branch arguments and stores them
into parameter slots within the active frame. Each slot is keyed by the
parameter's value identifier. When a block is entered these slots are copied
into the register file, making block parameters available as normal SSA values.
Blocks with no parameters skip this step to remain fast.
