//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/frontends/basic/Lowerer_Procedure.cpp
// Purpose: Stub file - implementation has been decomposed into focused modules.
//
// The procedure lowering implementation has been split into:
//
// - Lowerer_Procedure_Context.cpp
//     LoweringContext construction and symbol table management helpers.
//     Phase: Context Setup (runs before metadata collection)
//
// - Lowerer_Procedure_Signatures.cpp
//     Procedure signature collection and lookup.
//     Phase: Signature Collection (runs during program scanning)
//
// - Lowerer_Procedure_Variables.cpp
//     Variable discovery, type inference, and storage resolution.
//     Phase: Variable Collection (runs during metadata gathering)
//
// - Lowerer_Procedure_Skeleton.cpp
//     Block scheduling, skeleton construction, and slot allocation.
//     Phase: Block Scheduling (runs after metadata collection, before emission)
//
// - Lowerer_Procedure_Emit.cpp
//     Procedure body emission, parameter materialization, and state reset.
//     Phase: Emission (final phase of procedure lowering)
//
// Each file has a single, well-defined responsibility and documents which
// phase of the lowering pipeline it belongs to.
//
//===----------------------------------------------------------------------===//

// This file intentionally left empty - all implementation moved to:
// Lowerer_Procedure_Context.cpp, Lowerer_Procedure_Signatures.cpp,
// Lowerer_Procedure_Variables.cpp, Lowerer_Procedure_Skeleton.cpp,
// Lowerer_Procedure_Emit.cpp
