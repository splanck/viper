# Class Catalog

## Support layer

Utility classes for symbols, memory arenas, diagnostics, and other shared helpers.

## IL Core

Types, values, instructions, blocks, and modules that make up the in-memory IL
([IL spec](il-spec.md)).

## IL build/IO/verify

Builders, parsers, serializers, and the verifier used to construct and check
modules.

## Front end (BASIC)

Lexer, parser, AST, and lowering logic that turns BASIC source into IL.

## VM

Runtime structures like slots and frames plus the dispatch loop that interprets
IL.

## Codegen

Placeholder for future native backends and register allocation work.

## Tools

Command-line utilities for invoking the compiler, verifier, and runtime tests.
