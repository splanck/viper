' ai.bas - Simple chess AI using minimax with alpha-beta pruning
' Provides computer opponent
' Note: pieces.bas, board.bas, moves.bas are available via AddFile concatenation

CLASS ChessAI
    DIM Validator AS MoveValidator
    DIM BestFromRow AS INTEGER
    DIM BestFromCol AS INTEGER
    DIM BestToRow AS INTEGER
    DIM BestToCol AS INTEGER
    DIM NodesSearched AS INTEGER

    SUB New()
        Me.Validator = NEW MoveValidator()
        Me.BestFromRow = -1
        Me.BestFromCol = -1
        Me.BestToRow = -1
        Me.BestToCol = -1
        Me.NodesSearched = 0
    END SUB

    ' Evaluate the board position
    FUNCTION Evaluate(b AS Board) AS INTEGER
        DIM score AS INTEGER
        DIM r AS INTEGER
        DIM c AS INTEGER
        DIM piece AS INTEGER
        DIM ptype AS INTEGER
        DIM helper AS PieceHelper

        helper = NEW PieceHelper()
        score = 0

        FOR r = 0 TO 7
            FOR c = 0 TO 7
                piece = b.GetPiece(r, c)
                IF piece <> EMPTY THEN
                    ptype = helper.GetValue(piece)
                    IF piece > 0 THEN
                        score = score + ptype
                        ' Positional bonus for pawns
                        IF helper.GetType(piece) = PAWN THEN
                            score = score + r * 10
                        END IF
                        ' Center control bonus
                        IF r >= 3 AND r <= 4 AND c >= 3 AND c <= 4 THEN
                            score = score + 10
                        END IF
                    ELSE
                        score = score - ptype
                        ' Positional bonus for black pawns
                        IF helper.GetType(piece) = PAWN THEN
                            score = score - (7 - r) * 10
                        END IF
                        ' Center control bonus
                        IF r >= 3 AND r <= 4 AND c >= 3 AND c <= 4 THEN
                            score = score - 10
                        END IF
                    END IF
                END IF
            NEXT c
        NEXT r

        Evaluate = score
    END FUNCTION

    ' Minimax with alpha-beta pruning
    FUNCTION Minimax(b AS Board, depth AS INTEGER, alpha AS INTEGER, beta AS INTEGER, isMaximizing AS INTEGER) AS INTEGER
        DIM score AS INTEGER
        DIM bestScore AS INTEGER
        DIM fromRow AS INTEGER
        DIM fromCol AS INTEGER
        DIM toRow AS INTEGER
        DIM toCol AS INTEGER
        DIM piece AS INTEGER
        DIM savedFrom AS INTEGER
        DIM savedTo AS INTEGER
        DIM savedEnPassant AS INTEGER
        DIM skipFrom AS INTEGER
        DIM skipTo AS INTEGER
        DIM cutoff AS INTEGER

        Me.NodesSearched = Me.NodesSearched + 1

        ' Base case: evaluate position
        IF depth = 0 THEN
            Minimax = Me.Evaluate(b)
            EXIT FUNCTION
        END IF

        ' Check for checkmate or stalemate
        IF isMaximizing = 1 THEN
            IF Me.Validator.HasLegalMoves(b, 1) = 0 THEN
                IF Me.Validator.IsInCheck(b, 1) = 1 THEN
                    Minimax = -19000 - depth  ' Checkmate (prefer faster)
                ELSE
                    Minimax = 0  ' Stalemate
                END IF
                EXIT FUNCTION
            END IF
        ELSE
            IF Me.Validator.HasLegalMoves(b, 0) = 0 THEN
                IF Me.Validator.IsInCheck(b, 0) = 1 THEN
                    Minimax = 19000 + depth  ' Checkmate
                ELSE
                    Minimax = 0  ' Stalemate
                END IF
                EXIT FUNCTION
            END IF
        END IF

        cutoff = 0

        IF isMaximizing = 1 THEN
            bestScore = -30000

            FOR fromRow = 0 TO 7
                FOR fromCol = 0 TO 7
                    piece = b.GetPiece(fromRow, fromCol)
                    skipFrom = 0
                    IF piece <= 0 THEN skipFrom = 1

                    IF skipFrom = 0 AND cutoff = 0 THEN
                        FOR toRow = 0 TO 7
                            FOR toCol = 0 TO 7
                                skipTo = 0
                                IF Me.Validator.IsLegalMove(b, fromRow, fromCol, toRow, toCol, 1) = 0 THEN skipTo = 1

                                IF skipTo = 0 AND cutoff = 0 THEN
                                    ' Save state
                                    savedFrom = b.GetPiece(fromRow, fromCol)
                                    savedTo = b.GetPiece(toRow, toCol)
                                    savedEnPassant = b.EnPassantFile

                                    ' Make move
                                    b.SetPiece(toRow, toCol, savedFrom)
                                    b.SetPiece(fromRow, fromCol, EMPTY)
                                    b.EnPassantFile = -1

                                    ' Recurse
                                    score = Me.Minimax(b, depth - 1, alpha, beta, 0)

                                    ' Restore state
                                    b.SetPiece(fromRow, fromCol, savedFrom)
                                    b.SetPiece(toRow, toCol, savedTo)
                                    b.EnPassantFile = savedEnPassant

                                    IF score > bestScore THEN
                                        bestScore = score
                                    END IF
                                    IF score > alpha THEN
                                        alpha = score
                                    END IF
                                    IF beta <= alpha THEN
                                        cutoff = 1
                                    END IF
                                END IF
                            NEXT toCol
                        NEXT toRow
                    END IF
                NEXT fromCol
            NEXT fromRow

            Minimax = bestScore
        ELSE
            bestScore = 30000

            FOR fromRow = 0 TO 7
                FOR fromCol = 0 TO 7
                    piece = b.GetPiece(fromRow, fromCol)
                    skipFrom = 0
                    IF piece >= 0 THEN skipFrom = 1

                    IF skipFrom = 0 AND cutoff = 0 THEN
                        FOR toRow = 0 TO 7
                            FOR toCol = 0 TO 7
                                skipTo = 0
                                IF Me.Validator.IsLegalMove(b, fromRow, fromCol, toRow, toCol, 0) = 0 THEN skipTo = 1

                                IF skipTo = 0 AND cutoff = 0 THEN
                                    ' Save state
                                    savedFrom = b.GetPiece(fromRow, fromCol)
                                    savedTo = b.GetPiece(toRow, toCol)
                                    savedEnPassant = b.EnPassantFile

                                    ' Make move
                                    b.SetPiece(toRow, toCol, savedFrom)
                                    b.SetPiece(fromRow, fromCol, EMPTY)
                                    b.EnPassantFile = -1

                                    ' Recurse
                                    score = Me.Minimax(b, depth - 1, alpha, beta, 1)

                                    ' Restore state
                                    b.SetPiece(fromRow, fromCol, savedFrom)
                                    b.SetPiece(toRow, toCol, savedTo)
                                    b.EnPassantFile = savedEnPassant

                                    IF score < bestScore THEN
                                        bestScore = score
                                    END IF
                                    IF score < beta THEN
                                        beta = score
                                    END IF
                                    IF beta <= alpha THEN
                                        cutoff = 1
                                    END IF
                                END IF
                            NEXT toCol
                        NEXT toRow
                    END IF
                NEXT fromCol
            NEXT fromRow

            Minimax = bestScore
        END IF
    END FUNCTION

    ' Find the best move for the given side
    SUB FindBestMove(b AS Board, isWhiteTurn AS INTEGER, searchDepth AS INTEGER)
        DIM bestScore AS INTEGER
        DIM score AS INTEGER
        DIM fromRow AS INTEGER
        DIM fromCol AS INTEGER
        DIM toRow AS INTEGER
        DIM toCol AS INTEGER
        DIM piece AS INTEGER
        DIM savedFrom AS INTEGER
        DIM savedTo AS INTEGER
        DIM savedEnPassant AS INTEGER
        DIM alpha AS INTEGER
        DIM beta AS INTEGER
        DIM skipFrom AS INTEGER
        DIM skipTo AS INTEGER

        Me.NodesSearched = 0
        Me.BestFromRow = -1
        Me.BestFromCol = -1
        Me.BestToRow = -1
        Me.BestToCol = -1

        alpha = -30000
        beta = 30000

        IF isWhiteTurn = 1 THEN
            bestScore = -30000
        ELSE
            bestScore = 30000
        END IF

        FOR fromRow = 0 TO 7
            FOR fromCol = 0 TO 7
                piece = b.GetPiece(fromRow, fromCol)
                skipFrom = 0

                ' Skip empty and wrong color
                IF piece = EMPTY THEN skipFrom = 1
                IF skipFrom = 0 AND isWhiteTurn = 1 AND piece < 0 THEN skipFrom = 1
                IF skipFrom = 0 AND isWhiteTurn = 0 AND piece > 0 THEN skipFrom = 1

                IF skipFrom = 0 THEN
                    FOR toRow = 0 TO 7
                        FOR toCol = 0 TO 7
                            skipTo = 0
                            IF Me.Validator.IsLegalMove(b, fromRow, fromCol, toRow, toCol, isWhiteTurn) = 0 THEN skipTo = 1

                            IF skipTo = 0 THEN
                                ' Save state
                                savedFrom = b.GetPiece(fromRow, fromCol)
                                savedTo = b.GetPiece(toRow, toCol)
                                savedEnPassant = b.EnPassantFile

                                ' Make move
                                b.SetPiece(toRow, toCol, savedFrom)
                                b.SetPiece(fromRow, fromCol, EMPTY)
                                b.EnPassantFile = -1

                                ' Evaluate
                                IF isWhiteTurn = 1 THEN
                                    score = Me.Minimax(b, searchDepth - 1, alpha, beta, 0)
                                    IF score > bestScore THEN
                                        bestScore = score
                                        Me.BestFromRow = fromRow
                                        Me.BestFromCol = fromCol
                                        Me.BestToRow = toRow
                                        Me.BestToCol = toCol
                                    END IF
                                    IF score > alpha THEN alpha = score
                                ELSE
                                    score = Me.Minimax(b, searchDepth - 1, alpha, beta, 1)
                                    IF score < bestScore THEN
                                        bestScore = score
                                        Me.BestFromRow = fromRow
                                        Me.BestFromCol = fromCol
                                        Me.BestToRow = toRow
                                        Me.BestToCol = toCol
                                    END IF
                                    IF score < beta THEN beta = score
                                END IF

                                ' Restore state
                                b.SetPiece(fromRow, fromCol, savedFrom)
                                b.SetPiece(toRow, toCol, savedTo)
                                b.EnPassantFile = savedEnPassant
                            END IF
                        NEXT toCol
                    NEXT toRow
                END IF
            NEXT fromCol
        NEXT fromRow
    END SUB

    ' Get the best move found
    FUNCTION GetBestFromRow() AS INTEGER
        GetBestFromRow = Me.BestFromRow
    END FUNCTION

    FUNCTION GetBestFromCol() AS INTEGER
        GetBestFromCol = Me.BestFromCol
    END FUNCTION

    FUNCTION GetBestToRow() AS INTEGER
        GetBestToRow = Me.BestToRow
    END FUNCTION

    FUNCTION GetBestToCol() AS INTEGER
        GetBestToCol = Me.BestToCol
    END FUNCTION

    FUNCTION GetNodesSearched() AS INTEGER
        GetNodesSearched = Me.NodesSearched
    END FUNCTION
END CLASS
