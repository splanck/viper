'==============================================================================
' VIPER CHESS ENGINE v1.0
'==============================================================================
' A sophisticated chess engine written in Viper BASIC
'
' Features:
' - Complete chess rules (castling, en passant, promotion, etc.)
' - Alpha-beta search with iterative deepening
' - Quiescence search to avoid horizon effect
' - Move ordering (MVV-LVA, killer moves, history heuristic)
' - Piece-square tables for positional evaluation
' - Pawn structure analysis (doubled, isolated, passed pawns)
' - King safety and pawn shield evaluation
' - Bishop pair bonus, rook on open file bonus
' - Threefold repetition and 50-move rule detection
' - Insufficient material detection
' - Standard algebraic notation input/output
' - FEN position loading/saving
' - ANSI color board display
'
' Commands:
'   e2e4, Nf3  - Enter move in algebraic notation
'   new        - Start new game
'   flip       - Switch sides
'   fen        - Show current FEN
'   setfen X   - Set position from FEN
'   ai on/off  - Toggle AI
'   time N     - Set think time (seconds)
'   go         - Make AI play
'   undo       - Undo last move
'   moves      - Show legal moves
'   resign     - Resign game
'   quit       - Exit
'   help       - Show help
'
' Author: Viper BASIC Demo
' Uses: Viper.Text.StringBuilder, Viper.Time, Viper.Math
'==============================================================================

' Include all modules
ADDFILE "constants.bas"
ADDFILE "board.bas"
ADDFILE "movegen.bas"
ADDFILE "eval.bas"
ADDFILE "search.bas"
ADDFILE "game.bas"

' Main program entry point
SUB Main()
    ' Initialize all systems
    InitEval()
    InitBoard()
    InitSearch()
    InitGame()

    ' Run the game loop
    GameLoop()
END SUB

' Start the program
Main()
