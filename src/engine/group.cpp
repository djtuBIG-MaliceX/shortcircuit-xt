/*
 * Shortcircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2024, Various authors, as described in the github
 * transaction log.
 *
 * ShortcircuitXT is released under the Gnu General Public Licence
 * V3 or later (GPL-3.0-or-later). The license is found in the file
 * "LICENSE" in the root of this repository or at
 * https://www.gnu.org/licenses/gpl-3.0.en.html
 *
 * Individual sections of code which comprises ShortcircuitXT in this
 * repository may also be used under an MIT license. Please see the
 * section  "Licensing" in "README.md" for details.
 *
 * ShortcircuitXT is inspired by, and shares code with, the
 * commercial product Shortcircuit 1 and 2, released by VemberTech
 * in the mid 2000s. The code for Shortcircuit 2 was opensourced in
 * 2020 at the outset of this project.
 *
 * All source for ShortcircuitXT is available at
 * https://github.com/surge-synthesizer/shortcircuit-xt
 */

#include "group.h"
#include "bus.h"
#include "part.h"

#include "infrastructure/sse_include.h"

#include "sst/basic-blocks/mechanics/block-ops.h"
#include "sst/basic-blocks/dsp/PanLaws.h"

#include <cassert>

#include "messaging/messaging.h"
#include "patch.h"
#include "engine.h"
#include "group_and_zone_impl.h"

namespace scxt::engine
{

Group::Group() : id(GroupID::next()), name(id.to_string()), routingTable(modMatrix.routingTable)
{
    modMatrix.assignSourcesFromGroup(*this);
}

void Group::process(Engine &e)
{
    namespace blk = sst::basic_blocks::mechanics;

    mUILag.process();

    // TODO these memsets are probably gratuitous
    memset(output, 0, sizeof(output));

    // When we have alternate group routing we change these pointers
    float *lOut = output[0];
    float *rOut = output[1];

    modMatrix.copyBaseValuesFromGroup(*this);
    modMatrix.initializeModulationValues();

    if (anyModulatorUsed)
    {
        bool gated{false};
        for (const auto &z : zones)
        {
            gated = gated || (z->gatedVoiceCount > 0);
        }

        for (int i = 0; i < egPerGroup; ++i)
        {
            if (!gegUsed[i])
                continue;
            auto &eg = gegEvaluators[i];
            auto &egs = gegStorage[i];
            if (gated && eg.stage > ahdsrenv_t::s_hold)
            {
                eg.attackFrom(eg.outBlock0);
            }
            // fixme - put these all in the mod matrix
            eg.processBlock(egs.a, egs.h, egs.d, egs.s, egs.r, egs.aShape, egs.dShape, egs.rShape,
                            gated);
        }

        for (int i = 0; i < lfosPerGroup; ++i)
        {
            if (!lfoUsed[i])
                continue;
            stepLfos[i].process(blockSize);
        }

        modMatrix.process();
    }

    for (const auto &z : zones)
    {
        if (z->isActive())
        {
            z->process(e);
            /*
             * This is just an optimization to not accumulate. The zone will
             * have already routed to the approprite other bus and output will
             * be empty.
             */
            if (z->outputInfo.routeTo == DEFAULT_BUS)
            {
                blk::accumulate_from_to<blockSize>(z->output[0], lOut);
                blk::accumulate_from_to<blockSize>(z->output[1], rOut);
            }
        }
    }

    // for processor do the necessary
    for (int i = 0; i < engine::processorCount; ++i)
    {
        auto *p = processors[i];
        const auto &ps = processorStorage[i];
        float tempbuf alignas(16)[2][BLOCK_SIZE];

        if (p)
        {
            p->process_stereo(lOut, rOut, tempbuf[0], tempbuf[1], 0);
            processorMix[i].set_target(ps.mix);
            processorMix[i].fade_blocks(lOut, tempbuf[0], lOut);
            processorMix[i].fade_blocks(rOut, tempbuf[1], rOut);
        }
    }

    // Pan
    auto pvo = modMatrix.getValue(modulation::gmd_pan, 0);
    if (pvo != 0.f)
    {
        namespace pl = sst::basic_blocks::dsp::pan_laws;
        auto pv = (std::clamp(pvo, -1.f, 1.f) + 1) * 0.5;
        pl::panmatrix_t pmat{1, 1, 0, 0};
        pl::stereoEqualPower(pv, pmat);

        for (int i = 0; i < blockSize; ++i)
        {
            auto il = output[0][i];
            auto ir = output[1][i];
            output[0][i] = pmat[0] * il + pmat[2] * ir;
            output[1][i] = pmat[1] * ir + pmat[3] * il;
        }
    }

    // multiply by vca level from matrix
    auto mlev = modMatrix.getValue(modulation::gmd_grouplevel, 0);
    outputAmp.set_target(mlev * mlev * mlev);
    outputAmp.multiply_2_blocks(lOut, rOut);
}

void Group::addActiveZone()
{
    if (activeZones == 0)
    {
        parentPart->addActiveGroup();
    }
    activeZones++;
}

void Group::removeActiveZone()
{
    assert(activeZones);
    activeZones--;
    if (activeZones == 0)
    {
        parentPart->removeActiveGroup();
    }
}

engine::Engine *Group::getEngine()
{
    if (parentPart && parentPart->parentPatch)
        return parentPart->parentPatch->parentEngine;
    return nullptr;
}

void Group::setupOnUnstream(const engine::Engine &e)
{
    for (auto i = 0U; i < engine::lfosPerZone; ++i)
    {
        stepLfos[i].setSampleRate(sampleRate, sampleRateInv);

        stepLfos[i].assign(&modulatorStorage[i], modMatrix.getValuePtr(modulation::gmd_LFO_Rate, i),
                           nullptr, getEngine()->rngGen);
    }

    for (int p = 0; p < processorCount; ++p)
    {
        setupProcessorControlDescriptions(p, processorStorage[p].type);
        onProcessorTypeChanged(p, processorStorage[p].type);
    }

    modMatrix.copyBaseValuesFromGroup(*this);
    modMatrix.initializeModulationValues();
    modMatrix.updateModulatorUsed(*this);
    for (auto &lfo : stepLfos)
    {
        lfo.UpdatePhaseIncrement();
    }
}
void Group::onSampleRateChanged()
{
    for (auto i = 0U; i < engine::lfosPerZone; ++i)
    {
        stepLfos[i].setSampleRate(sampleRate, sampleRateInv);

        stepLfos[i].assign(&modulatorStorage[i], modMatrix.getValuePtr(modulation::gmd_LFO_Rate, i),
                           nullptr, getEngine()->rngGen);
    }

    for (auto p : processors)
    {
        if (p)
        {
            p->setSampleRate(sampleRate, sampleRateInv);
        }
    }
}

void Group::onProcessorTypeChanged(int w, dsp::processor::ProcessorType t)
{
    if (t != dsp::processor::ProcessorType::proct_none)
    {
        SCLOG("Group Processor Changed: " << w << " " << t);
        // FIXME - replace the float params with something modulatable
        processors[w] = dsp::processor::spawnProcessorInPlace(
            t, asT()->getEngine()->getMemoryPool().get(), processorPlacementStorage[w],
            dsp::processor::processorMemoryBufferSize, processorStorage[w].floatParams.data(),
            processorStorage[w].intParams.data());

        processors[w]->setSampleRate(sampleRate);
        processors[w]->init();
    }
    else
    {
        if (processors[w])
        {
            dsp::processor::unspawnProcessor(processors[w]);
            processors[w] = nullptr;
        }
    }
}
template struct HasGroupZoneProcessors<Group>;
} // namespace scxt::engine