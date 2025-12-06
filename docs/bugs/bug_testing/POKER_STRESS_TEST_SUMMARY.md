# Texas Hold'em Poker - VIPER BASIC OOP Stress Test

**Date**: 2025-11-16
**Objective**: Stress test VIPER BASIC OOP capabilities by building a complex poker game with multiple classes, ANSI graphics, and sophisticated object interactions.

---

## Test Files Created (Progressive Development)

### Phase 1: Card Class
- **poker_v1_card_class.bas** ✅ **SUCCESS**
  - Basic Card class with suit/rank fields
  - Methods: Init(), GetSuitName(), GetRankName(), Display()
  - ANSI color support (red for hearts/diamonds, white for spades/clubs)
  - Unicode suit symbols (♠♥♦♣)
  - **Result**: Works perfectly

### Phase 2: Deck Class - Bug Discovery
- **poker_v2_deck_class.bas** ❌ **FAILED - BUG-074 DISCOVERED**
  - Added Deck class after Card class
  - Deck.ShowCard() creates Card objects
  - **Error**: `DECK.__ctor:entry_DECK.__ctor: call %t27: unknown temp %27`
  - **Discovery**: Constructor corrupted with string cleanup from wrong scope

- **poker_v3_simple_deck.bas** ❌ **FAILED - Confirmed BUG-074**
  - Simplified version to isolate the bug
  - Same constructor corruption error
  - **Conclusion**: Bug triggered when class B uses class A defined earlier

### Phase 3: Bug Investigation
- **test_empty_class.bas** ✅ Works
- **test_methods_no_fields.bas** ✅ Works
- **test_two_classes.bas** ✅ Works
- **test_object_in_method.bas** ✅ Works
- **test_me_method_calls.bas** ✅ Works
- **poker_v4_reversed_order.bas** ✅ **WORKAROUND FOUND**
  - Defined Deck BEFORE Card (forward reference)
  - **Result**: Works perfectly!
  - **Workaround**: Define classes in reverse dependency order

### Phase 4: Working Implementations
- **poker_v5_working_deck.bas** ✅ **SUCCESS**
  - Full 52-card deck with Deck defined before Card
  - Methods: GetCardSuit(), GetCardRank(), ShowCard(), ShowAllCards()
  - Displays all suits correctly with colors
  - **Result**: Perfect deck visualization

- **poker_v6_player_class.bas** ✅ **SUCCESS**
  - Player class with hand management
  - Fields: name, chips, card1/card2 (workaround for BUG-067)
  - Methods: Init(), GiveCard(), ShowHand(), ClearHand(), Bet()
  - Deck dealing system
  - **Result**: Complete player and dealing system

- **poker_v7_table.bas** ✅ **SUCCESS**
  - Table class managing community cards and pot
  - Community cards: flop (3 cards), turn (1 card), river (1 card)
  - Beautiful ANSI boxed display with colors
  - Full poker hand simulation (pre-flop → flop → turn → river → showdown)
  - **Result**: Complete working poker table!

---

## New Bug Documented

### **BUG-074 CRITICAL**: Constructor Corruption When Class Uses Previously-Defined Class

**Symptom**: When a class B uses another class A that was defined earlier in the source file, the constructor for class B becomes corrupted with string cleanup code from unrelated contexts, causing "use before def" errors.

**IL Evidence**:
```
func @DECK.__ctor(ptr %ME) -> void {
entry_DECK.__ctor(%ME:ptr):
  %t1 = alloca 8
  store ptr, %t1, %t0        // ERROR: %t0 undefined, should be %ME
  %t2 = load ptr, %t1
  br ret_DECK.__ctor
ret_DECK.__ctor:
  call @rt_str_release_maybe(%t27)  // ERROR: %t27 from calling context!
  call @rt_str_release_maybe(%t30)  // ERROR: %t30 from calling context!
  ret
}
```

**Workaround**: Define classes in reverse dependency order - define the "using" class before the "used" class. Forward references work correctly.

**Impact**: CRITICAL - Severely restricts multi-class programs. Must use counterintuitive class ordering.

**Documentation**: Added to `/bugs/basic_bugs.md` as BUG-074 with full details, examples, and IL evidence.

---

## Features Successfully Stress Tested

### OOP Features
✅ Multiple classes in one file (4 classes: Card, Deck, Player, Table)
✅ Object creation in methods
✅ Method calls on ME (self)
✅ Multiple object instances
✅ Integer fields in classes
✅ String fields in classes
✅ Methods with multiple parameters (up to 6 parameters tested)
✅ Functions returning INTEGER
✅ Functions returning STRING
✅ Object parameters (workaround for BUG-073 avoided)

### Language Features
✅ ANSI colors (COLOR command) - Multiple colors used
✅ Complex PRINT statements with semicolons for inline printing
✅ Unicode symbols (♠♥♦♣) in strings
✅ FOR loops with objects
✅ FOR loops with method calls inside
✅ Passing loop variables to methods
✅ Nested IF-ELSEIF-END IF chains (up to 14 branches)
✅ Integer division (\)
✅ Integer subtraction and multiplication
✅ Arithmetic operations
✅ Comparison operators (<=, >=, =, OR, AND)

### Graphics/Display
✅ Box-drawing characters (╔ ╗ ║ ═ ╠ ╣ ╚ ─)
✅ Colored output for suits (red for hearts/diamonds, white for spades/clubs)
✅ Colored output for UI elements (cyan, yellow, green)
✅ Complex formatted displays with alignment

---

## Known Bugs Worked Around

1. **BUG-067**: Array Fields in Classes Not Supported
   - **Workaround**: Used multiple scalar fields (card1Suit, card1Rank, card2Suit, card2Rank) instead of arrays

2. **BUG-069**: Objects Not Initialized in Constructor
   - **Workaround**: Avoided object fields where possible, used calculated values

3. **BUG-074**: Constructor Corruption (NEW - discovered during this test)
   - **Workaround**: Defined classes in reverse dependency order (Deck before Card, etc.)

---

## Additional FOR Loop Tests

### test_for_loop_with_classes.bas ✅
- Tests FOR loops with objects
- Tests method calls inside FOR loops
- Tests multiple objects in FOR loops
- **Result**: All scenarios work correctly

### test_loop_var_method.bas ✅
- Tests passing loop variable to method as parameter
- **Result**: Works correctly

### test_for_print_bug.bas
- Tests PRINT statements in FOR loops
- Tests COLOR changes in FOR loops
- (Not run in this session, but available for testing)

---

## Final Program Statistics (poker_v7_table.bas)

- **Total Lines**: 398 lines
- **Classes**: 4 (Table, Player, Deck, Card)
- **Methods**: 18 methods across all classes
- **Functions**: 7 functions
- **Objects Created**: 5 objects in main program
- **Colors Used**: 5 different colors (10=green, 11=cyan, 12=red, 14=yellow, 15=white)
- **Unicode Symbols**: 4 card suits
- **Box Drawing Chars**: 8 different characters

---

## Conclusion

The Texas Hold'em Poker stress test successfully demonstrated that VIPER BASIC can handle complex OOP programs with multiple classes, sophisticated object interactions, and rich ANSI graphics. The test discovered one NEW CRITICAL BUG (BUG-074) and successfully worked around three known bugs.

The final poker table program displays a complete Texas Hold'em game with:
- Player hands with chip counts
- Community cards (flop, turn, river)
- Pot management
- Betting system
- Beautiful colored ANSI display
- Professional-looking box UI

**Overall Assessment**: VIPER BASIC OOP is functional but requires workarounds for known bugs. With proper understanding of the limitations, complex programs can be successfully built.
