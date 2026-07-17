//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/il/verify/EhModel.cpp
//
// Purpose:
//   Provide the concrete implementation of the EhModel helper which captures
//   a function's exception-handling structure for downstream verification
//   passes.
//
// Key invariants:
//   * The model does not mutate the underlying function.
//   * Successor queries rely on the label map populated during construction.
//
// Ownership/Lifetime:
//   The EhModel stores raw pointers to IR nodes owned by the caller and expects
//   them to remain valid for the model's lifetime.
//
// Links:
//   docs/il/il-guide.md#reference
//
//===----------------------------------------------------------------------===//

#include "il/verify/EhModel.hpp"

#include "il/verify/ControlFlowChecker.hpp"

#include <algorithm>
#include <cstddef>

using namespace il::core;

namespace il::verify {

/// @brief Capture exception-handling structure for @p function.
/// @details Builds label lookups for all basic blocks and records the entry
///          block so later analyses can answer reachability queries without
///          recomputing metadata.  The model stores raw pointers into the
///          original function and therefore must not outlive it.
/// @param function Function whose EH layout should be modelled.
EhModel::EhModel(const Function &function) : fn(&function) {
    if (!function.blocks.empty())
        entryBlock = &function.blocks.front();

    blocks.reserve(function.blocks.size());
    for (const auto &block : function.blocks) {
        // Use emplace with string_view key referencing block.label.
        // The Function must outlive this EhModel for the view to remain valid.
        blocks.emplace(std::string_view{block.label}, &block);
    }

    for (const auto &block : function.blocks) {
        if (hasEh)
            break;

        for (const auto &instr : block.instructions) {
            switch (instr.op) {
                case Opcode::EhPush:
                case Opcode::EhPop:
                case Opcode::EhEntry:
                case Opcode::ResumeSame:
                case Opcode::ResumeNext:
                case Opcode::ResumeLabel:
                case Opcode::Trap:
                case Opcode::TrapFromErr:
                    hasEh = true;
                    break;
                default:
                    break;
            }
            if (hasEh)
                break;
        }
    }

    for (const auto &block : function.blocks) {
        for (const auto &instr : block.instructions) {
            if (instr.op != Opcode::EhPush || instr.labels.empty())
                continue;

            EhHandlerPushSite site;
            site.id = handlerPushSites.size();
            site.block = &block;
            site.instr = &instr;
            site.handler = findBlock(instr.labels[0]);
            pushSiteByInstr.emplace(&instr, site.id);
            handlerPushSites.push_back(site);
        }
    }
}

/// @brief Locate a basic block by its label.
/// @details Consults the pre-built label map and returns the corresponding
///          basic-block pointer when it exists.  Missing labels yield @c nullptr
///          so callers can report diagnostics without dereferencing invalid
///          pointers.
/// @param label Name of the basic block to retrieve.
/// @return Pointer to the block when present, otherwise nullptr.
const BasicBlock *EhModel::findBlock(std::string_view label) const {
    auto it = blocks.find(label);
    if (it == blocks.end())
        return nullptr;
    return it->second;
}

/// @brief Enumerate successor blocks referenced by a terminator instruction.
/// @details Handles the various terminator flavours used by the IL (branch,
///          conditional branch, switch, resume variants, and trap).  Labels are
///          resolved through @ref findBlock so downstream checks receive direct
///          block pointers.  Missing labels are ignored to keep verification
///          resilient to malformed modules.
/// @param terminator Terminator instruction whose outgoing edges are requested.
/// @return Vector containing zero or more successor block pointers.
std::vector<const BasicBlock *> EhModel::gatherSuccessors(const Instr &terminator) const {
    std::vector<const BasicBlock *> successors;
    for (const EhSuccessorEdge &edge : gatherSuccessorEdges(terminator)) {
        if (edge.target)
            successors.push_back(edge.target);
    }
    return successors;
}

/// @brief Enumerate resolved EH-aware successor edges for @p terminator.
/// @details This variant preserves the label index so callers can inspect the
///          corresponding branch argument bundle and distinguish ordinary
///          control flow from `resume.label` transfers.
std::vector<EhSuccessorEdge> EhModel::gatherSuccessorEdges(const Instr &terminator) const {
    std::vector<EhSuccessorEdge> successors;
    auto addEdge = [&](std::size_t labelIndex, EhEdgeKind kind) {
        if (labelIndex >= terminator.labels.size())
            return;
        if (const BasicBlock *target = findBlock(terminator.labels[labelIndex]))
            successors.push_back(EhSuccessorEdge{target, labelIndex, kind});
    };

    switch (terminator.op) {
        case Opcode::Br:
            addEdge(0, EhEdgeKind::Normal);
            break;
        case Opcode::CBr:
        case Opcode::SwitchI32:
            for (std::size_t labelIndex = 0; labelIndex < terminator.labels.size(); ++labelIndex)
                addEdge(labelIndex, EhEdgeKind::Normal);
            break;
        case Opcode::ResumeLabel:
            addEdge(0, EhEdgeKind::Resume);
            break;
        default:
            break;
    }
    return successors;
}

/// @brief Retrieve the terminator instruction for @p block.
/// @details Scans the block's instruction list and returns the first
///          instruction classified as a terminator.  Non-terminating blocks
///          yield @c nullptr, allowing callers to differentiate between
///          fallthrough and explicit control transfers.
/// @param block Basic block whose terminator is requested.
/// @return Pointer to the terminator instruction, or nullptr when absent.
const Instr *EhModel::findTerminator(const BasicBlock &block) const {
    for (const auto &instr : block.instructions) {
        if (isTerminator(instr.op))
            return &instr;
    }
    return nullptr;
}

/// @brief Identify handler-shaped blocks by their leading marker.
/// @details Signature validation is performed elsewhere; this helper is a
///          lightweight CFG classifier for EH provenance and edge checks.
bool EhModel::isHandlerBlock(const BasicBlock &block) const noexcept {
    return !block.instructions.empty() && block.instructions.front().op == Opcode::EhEntry;
}

/// @brief Return the resume-token parameter id for canonical handlers.
/// @details Only the standard two-parameter `Error`/`ResumeTok` handler ABI
///          produces an id. Helper-shaped or malformed blocks return no value
///          so callers can defer to existing structural diagnostics.
std::optional<unsigned> EhModel::handlerResumeTokenParam(const BasicBlock &block) const noexcept {
    if (!isHandlerBlock(block) || block.params.size() < 2)
        return std::nullopt;
    if (block.params[0].type.kind != Type::Kind::Error ||
        block.params[1].type.kind != Type::Kind::ResumeTok)
        return std::nullopt;
    return block.params[1].id;
}

/// @brief Look up push-site metadata for a known instruction address.
/// @details The model indexes `eh.push` instructions during construction using
///          addresses from the borrowed function. A missing entry means the
///          instruction was not a well-formed push in this model.
const EhHandlerPushSite *EhModel::findPushSite(const Instr &instr) const noexcept {
    auto it = pushSiteByInstr.find(&instr);
    if (it == pushSiteByInstr.end() || it->second >= handlerPushSites.size())
        return nullptr;
    return &handlerPushSites[it->second];
}

} // namespace il::verify
