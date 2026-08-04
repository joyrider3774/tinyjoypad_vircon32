#ifndef AVRCOMPAT_H
#define AVRCOMPAT_H

// -----------------------------------------------------------------------------
// Small compatibility layer so upstream TinyJoypad game sources (written
// against AVR/Arduino headers this project never includes) can be ported
// with minimal line-by-line editing.
//
// Vircon32 has only 4 primitive types, all one 32-bit word (int/float/bool/
// void - see VIRCON32_C_DIALECT.md section 2) and flat, byte-addressed... no,
// WORD-addressed memory (section 7) with no separate flash address space.
// Aliasing every AVR fixed-width int type to plain `int` costs range/packing
// but nothing else: any upstream array indexing or memcpy_P() call written
// as `sizeof(x)` (in bytes, of a uint8_t array) still produces the exact
// same numeric element count once that array becomes `int[N]` and sizeof
// counts words instead - the two units change together, so the arithmetic
// keeps working unmodified. Only code that hardcodes a *different* byte vs.
// element count (rare - basically only true bit-packed/byte-serialization
// tricks) needs a manual fix; that gets caught case-by-case while porting.
//
// PROGMEM/pgm_read_*/memcpy_P exist on AVR only to reach a separate flash
// address space; Vircon32 has one flat address space, so every one of
// these becomes a plain, ordinary access.
// -----------------------------------------------------------------------------

typedef int uint8_t;
typedef int int8_t;
typedef int uint16_t;
typedef int int16_t;
typedef int uint32_t;
typedef int int32_t;
typedef int size_t;

#define PROGMEM

#define pgm_read_byte(addr)  (*(addr))
#define pgm_read_word(addr)  (*(addr))
#define pgm_read_dword(addr) (*(addr))

#define memcpy_P memcpy
#define memcmp_P memcmp
#define strcpy_P strcpy
#define strcmp_P strcmp
#define strlen_P strlen

// Arduino's "flash string" helper - Vircon32 has no separate flash address
// space, so a flash string is just a normal int[] string literal.
#define __FlashStringHelper int
#define F(s) s

#define bit(n)            ( 1 << (n) )
#define bitRead(value, n) ( ( (value) >> (n) ) & 1 )
#define bitSet(value, n)  ( (value) |= ( 1 << (n) ) )
#define bitClear(value, n) ( (value) &= ~( 1 << (n) ) )

// AVR's rand() (Arduino/avr-libc) returns a 15-bit non-negative value
// (0..32767). Vircon32's rand() (misc.h) instead returns the raw 32-bit RNG
// register directly - any sign, any magnitude. Code ported verbatim from
// AVR that does `rand() % n` (or worse, arithmetic assuming a ~16-bit
// range, like a hollow-distance formula that adds a fixed offset then
// shifts right) silently breaks: a negative raw value modulo n can itself
// be negative in C, and Vircon32's `>>` is a *logical* shift (no sign
// extension - see VIRCON32_C_DIALECT.md section 6), so shifting a negative
// value right produces a huge positive result instead of a small one.
// (This was a real bug: hollowseeker's cave-hollow-frequency formula
// depended on AVR's rand() range, and on Vircon32 it produced enormous
// results, so the "make a gap" branch almost never fired and the cave
// filled in solid over time.) Use this instead of `rand() % n` anywhere
// a bounded, non-negative random value is needed - safe regardless of
// what rand() itself returns.
int arand( int n )
{
    if( n <= 0 )
      return 0;

    int r = rand();
    if( r < 0 )
      r = -r;

    return r % n;
}

#endif
