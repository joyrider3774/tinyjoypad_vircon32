// =============================================================================
// eepromShim.c - see eepromShim.h for the public API and its own rationale.
// =============================================================================
// On-card layout: word 0-19 is portVircon32.c's own fixed cardSignature
// (identifies "this card was written by tinyjoypad_vircon32" as a whole,
// the same discipline crisp-game-lib-portable_vircon32 already uses for its
// own single-game save). Word 20 onward is a flat MAX_GAMES-length table of
// struct EepromSlot, slot N at word offset EEPROM_TABLE_START_WORD +
// N*EEPROM_SLOT_WORDS.
//
// **Looked up by name, not registration index** - a game's own slot is
// found (or claimed, the first time it's ever selected) via an open-
// addressing hash table: eepromHashTitle(title) % MAX_GAMES picks a
// starting probe slot, then eepromSelectGame() scans forward (wrapping)
// until it finds either a slot whose stored nameTag already matches this
// exact title (reuse it) or a genuinely empty slot (claim it). This means
// a game's own save data always lands wherever its *name* hashes to,
// regardless of where it sits in menuGameList.c's own addGame() call
// order - reordering the menu, or inserting a new game between two
// existing ones, can never silently swap or corrupt an unrelated game's
// own save, the way indexing directly by registration index would risk.
// Open addressing guarantees zero collisions between any two distinct
// titles as long as the table isn't completely full - trivially true here
// (MAX_GAMES=64, defined in menu.c and already in scope by the time this
// file is included from main.c; well under 64 games actually use this
// shim).
//
// Every conceptual "AVR EEPROM byte" is stored as one full Vircon32 int
// cell (matching avrCompat.h's own project-wide choice to widen every
// uint8_t to a plain int rather than pack bytes) - the original AVR hi-
// byte/lo-byte splitting each upstream game's own eeprom_read_word()-style
// call performed is a hardware artifact, not something worth preserving
// bit-for-bit; only the *behavior* (does the value survive a reboot)
// needs to match, the same "preserve behavior, not implementation
// quirks" precedent already established for the byte-truncation/shift-
// wraparound/signed-sentinel bug family documented in CLAUDE.md.
//
// **Fresh/never-written cells default to 255 (0xFF), not 0** - matching
// real AVR EEPROM's own actual factory-erased state, not an arbitrary
// zero. This matters: several upstream games explicitly check for a
// literal 255 as their own "this EEPROM has never been written" sentinel
// (e.g. attiny85-flappy-bird's own `if (high_score == 255) high_score =
// 0;`, ATtiny Tetromino's own `if (top == 255) ...`) - defaulting to 0
// instead would have silently changed what those checks actually detect.
// =============================================================================

#define EEPROM_TAG_WORDS 32
#define EEPROM_SLOT_DATA_SIZE 512
#define EEPROM_MAGIC 0x45455032

#define EEPROM_MAGIC_OFFSET ( EEPROM_TAG_WORDS )
#define EEPROM_CHECKSUM_OFFSET ( EEPROM_TAG_WORDS + 1 )
#define EEPROM_DATA_OFFSET ( EEPROM_TAG_WORDS + 2 )
#define EEPROM_SLOT_WORDS ( EEPROM_TAG_WORDS + 2 + EEPROM_SLOT_DATA_SIZE )

#define EEPROM_TABLE_START_WORD 20

struct EepromSlot
{
    int[ EEPROM_TAG_WORDS ] nameTag;
    int magic;
    int checksum;
    int[ EEPROM_SLOT_DATA_SIZE ] data;
};

EepromSlot currentSlot;
int currentSlotOffset;
bool eepromCardAvailable;

int eepromHashTitle( int* title )
{
    int hash = 0;
    int i = 0;
    while( title[ i ] != 0 )
    {
        hash = hash * 31 + title[ i ];
        i++;
    }
    if( hash < 0 ) hash = -hash;
    return hash;
}

int eepromCalcChecksum()
{
    int sum = 0;
    int i;
    for( i = 0; i < EEPROM_SLOT_DATA_SIZE; i++ ) sum += currentSlot.data[ i ];
    return sum;
}

void eepromResetCurrentSlotToFresh( int* title )
{
    int i;
    for( i = 0; i < EEPROM_TAG_WORDS; i++ ) currentSlot.nameTag[ i ] = 0;
    strcpy( currentSlot.nameTag, title );
    currentSlot.magic = EEPROM_MAGIC;
    for( i = 0; i < EEPROM_SLOT_DATA_SIZE; i++ ) currentSlot.data[ i ] = 255;
    currentSlot.checksum = eepromCalcChecksum();
}

void eepromSelectGame( int* title )
{
    eepromResetCurrentSlotToFresh( title );
    eepromCardAvailable = false;
    currentSlotOffset = -1;

    if( !md_cardIsConnected() ) return;

    // Stamp a fresh/foreign card once, the first time anything ever tries
    // to use it - every slot past the signature on a freshly-stamped card
    // reads back as real card memory (assumed zeroed on a truly blank
    // card, matching this SDK's own documented "all-zero signature means
    // empty" convention from memcard.h's card_is_empty()), which is not
    // EEPROM_MAGIC, so the "is this slot empty" check below already
    // handles it correctly with no special-casing needed here.
    if( !md_cardHasOurSignature() ) md_cardWriteSignature();

    int startSlot = eepromHashTitle( title ) % MAX_GAMES;
    int probe;
    for( probe = 0; probe < MAX_GAMES; probe++ )
    {
        int slotIndex = ( startSlot + probe ) % MAX_GAMES;
        int slotOffset = EEPROM_TABLE_START_WORD + slotIndex * EEPROM_SLOT_WORDS;

        EepromSlot candidate;
        md_cardReadData( &candidate, slotOffset, EEPROM_SLOT_WORDS );

        bool isEmpty = ( candidate.magic != EEPROM_MAGIC );

        if( !isEmpty && strcmp( candidate.nameTag, title ) == 0 )
        {
            currentSlot = candidate;
            currentSlotOffset = slotOffset;
            eepromCardAvailable = true;

            if( currentSlot.checksum != eepromCalcChecksum() )
            {
                // corrupted or torn write - start this game fresh rather
                // than trust garbage data
                eepromResetCurrentSlotToFresh( title );
                md_cardWriteData( &currentSlot, slotOffset, EEPROM_SLOT_WORDS );
            }
            return;
        }

        if( isEmpty )
        {
            eepromResetCurrentSlotToFresh( title );
            currentSlotOffset = slotOffset;
            eepromCardAvailable = true;
            md_cardWriteData( &currentSlot, slotOffset, EEPROM_SLOT_WORDS );
            return;
        }

        // occupied by a different game's own tag - keep probing
    }

    // table is completely full (should never happen at MAX_GAMES=64 with
    // well under 64 real games using this shim) - fall back to no
    // persistence this session rather than loop forever; eepromCardAvailable
    // stays false, so every write below silently no-ops.
}

int eeprom_read_byte( int address )
{
    if( address < 0 || address >= EEPROM_SLOT_DATA_SIZE ) return 255;
    return currentSlot.data[ address ];
}

void eeprom_write_byte( int address, int value )
{
    if( !eepromCardAvailable ) return;
    if( address < 0 || address >= EEPROM_SLOT_DATA_SIZE ) return;

    currentSlot.data[ address ] = value;
    currentSlot.checksum = eepromCalcChecksum();

    md_cardWriteData( &currentSlot.data[ address ], currentSlotOffset + EEPROM_DATA_OFFSET + address, 1 );
    md_cardWriteData( &currentSlot.checksum, currentSlotOffset + EEPROM_CHECKSUM_OFFSET, 1 );
}

void eeprom_update_byte( int address, int value )
{
    if( address < 0 || address >= EEPROM_SLOT_DATA_SIZE ) return;
    if( currentSlot.data[ address ] == value ) return;
    eeprom_write_byte( address, value );
}

// Treated as two/four consecutive byte cells (matching how eeprom_read_block()
// below treats any run of addresses) - each real upstream call site already
// does its own hi/lo combining, so this shim doesn't need to guess or
// preserve any particular endianness convention.
int eeprom_read_word( int address )
{
    int hi = eeprom_read_byte( address );
    int lo = eeprom_read_byte( address + 1 );
    return ( hi << 8 ) | ( lo & 0xFF );
}

void eeprom_write_word( int address, int value )
{
    eeprom_write_byte( address, ( value >> 8 ) & 0xFF );
    eeprom_write_byte( address + 1, value & 0xFF );
}

int eeprom_read_dword( int address )
{
    int b0 = eeprom_read_byte( address );
    int b1 = eeprom_read_byte( address + 1 );
    int b2 = eeprom_read_byte( address + 2 );
    int b3 = eeprom_read_byte( address + 3 );
    return ( b0 << 24 ) | ( ( b1 & 0xFF ) << 16 ) | ( ( b2 & 0xFF ) << 8 ) | ( b3 & 0xFF );
}

void eeprom_write_dword( int address, int value )
{
    eeprom_write_byte( address, ( value >> 24 ) & 0xFF );
    eeprom_write_byte( address + 1, ( value >> 16 ) & 0xFF );
    eeprom_write_byte( address + 2, ( value >> 8 ) & 0xFF );
    eeprom_write_byte( address + 3, value & 0xFF );
}

void eeprom_read_block( void* dest, int address, int size )
{
    int* destInts = (int*)dest;
    int i;
    for( i = 0; i < size; i++ ) destInts[ i ] = eeprom_read_byte( address + i );
}

void eeprom_write_block( void* src, int address, int size )
{
    int* srcInts = (int*)src;
    int i;
    for( i = 0; i < size; i++ ) eeprom_write_byte( address + i, srcInts[ i ] );
}

void eeprom_busy_wait()
{
    // no-op - Vircon32's memory card has no write latency to wait out
}
