/*
 * Shortcircuit XT - a Surge Synth Team product
 *
 * A fully featured creative sampler, available as a standalone
 * and plugin for multiple platforms.
 *
 * Copyright 2019 - 2023, Various authors, as described in the github
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

#include "selection_manager.h"
#include <iostream>
#include "engine/engine.h"
#include "engine/patch.h"
#include "messaging/client/selection_messages.h"
#include "messaging/messaging.h"
#include "messaging/client/client_serial.h"
#include "messaging/client/processor_messages.h"

#define DEBUG_SELECTION 0

namespace scxt::selection
{
namespace cms = messaging::client;

void SelectionManager::sendClientDataForSelectionState()
{
    auto [p, g, z] = leadZone;

    // Check if I deserialized a valid zone and adjust if not
    if (p >= 0)
    {
        selectedPart = p;
        if (p >= engine.getPatch()->numParts)
        {
            p = -1;
            g = -1;
            z = -1;
        }
        else if (g >= 0)
        {
            if (g >= engine.getPatch()->getPart(p)->getGroups().size())
            {
                g = -1;
                z = -1;
            }
            else if (z >= 0)
            {
                if (z >= engine.getPatch()->getPart(p)->getGroup(g)->getZones().size())
                {
                    z = -1;
                }
            }
        }
    }

    if (p >= 0 && g >= 0)
    {
        serializationSendToClient(cms::s2c_send_selected_group_zone_mapping_summary,
                                  engine.getPatch()->getPart(p)->getZoneMappingSummary(),
                                  *(engine.getMessageController()));
    }
    if (z >= 0 && g >= 0 && p >= 0)
    {
        // TODO: The 'full zone' becomes a single function obviously
        const auto &zp = engine.getPatch()->getPart(p)->getGroup(g)->getZone(z);
        serializationSendToClient(cms::s2c_respond_zone_mapping,
                                  cms::MappingSelectedZoneView::s2c_payload_t{true, zp->mapping},
                                  *(engine.getMessageController()));
        serializationSendToClient(cms::s2c_respond_zone_samples,
                                  cms::SampleSelectedZoneView::s2c_payload_t{true, zp->sampleData},
                                  *(engine.getMessageController()));
        serializationSendToClient(cms::s2c_respond_zone_adsr_view,
                                  cms::AdsrSelectedZoneView::s2c_payload_t{0, true, zp->aegStorage},
                                  *(engine.getMessageController()));
        serializationSendToClient(cms::s2c_respond_zone_adsr_view,
                                  cms::AdsrSelectedZoneView::s2c_payload_t{1, true, zp->eg2Storage},
                                  *(engine.getMessageController()));

        for (int i = 0; i < engine::processorsPerZone; ++i)
        {
            serializationSendToClient(
                cms::s2c_respond_single_processor_metadata_and_data,
                cms::ProcessorMetadataAndData::s2c_payload_t{i, true, zp->processorDescription[i],
                                                             zp->processorStorage[i]},
                *(engine.getMessageController()));
        }

        for (int i = 0; i < engine::lfosPerZone; ++i)
        {
            serializationSendToClient(cms::s2c_update_zone_individual_lfo,
                                      cms::indexedLfoUpdate_t{true, i, zp->lfoStorage[i]},
                                      *(engine.getMessageController()));
        }
        serializationSendToClient(cms::s2c_update_zone_voice_matrix_metadata,
                                  modulation::getVoiceModMatrixMetadata(*zp),
                                  *(engine.getMessageController()));
        serializationSendToClient(cms::s2c_update_zone_voice_matrix, zp->routingTable,
                                  *(engine.getMessageController()));
    }
    else
    {
        serializationSendToClient(cms::s2c_respond_zone_mapping,
                                  cms::MappingSelectedZoneView::s2c_payload_t{false, {}},
                                  *(engine.getMessageController()));

        serializationSendToClient(cms::s2c_respond_zone_samples,
                                  cms::SampleSelectedZoneView::s2c_payload_t{false, {}},
                                  *(engine.getMessageController()));
        serializationSendToClient(cms::s2c_respond_zone_adsr_view,
                                  cms::AdsrSelectedZoneView::s2c_payload_t{0, false, {}},
                                  *(engine.getMessageController()));
        serializationSendToClient(cms::s2c_respond_zone_adsr_view,
                                  cms::AdsrSelectedZoneView::s2c_payload_t{1, false, {}},
                                  *(engine.getMessageController()));
        for (int i = 0; i < engine::processorsPerZone; ++i)
        {
            serializationSendToClient(
                cms::s2c_respond_single_processor_metadata_and_data,
                cms::ProcessorMetadataAndData::s2c_payload_t{i, false, {}, {}},
                *(engine.getMessageController()));
        }
        serializationSendToClient(cms::s2c_update_zone_voice_matrix_metadata,
                                  modulation::voiceModMatrixMetadata_t{false, {}, {}, {}},
                                  *(engine.getMessageController()));
    }
}

void SelectionManager::multiSelectAction(const std::vector<SelectActionContents> &v)
{
    for (const auto &z : v)
        adjustInternalStateForAction(z);
    guaranteeSelectedLead();
    sendClientDataForSelectionState();
    sendSelectedZonesToClient();
    debugDumpSelectionState();
}

void SelectionManager::selectAction(
    const scxt::selection::SelectionManager::SelectActionContents &z)
{
    adjustInternalStateForAction(z);
    guaranteeSelectedLead();
    sendClientDataForSelectionState();
    sendSelectedZonesToClient();
    debugDumpSelectionState();
}

void SelectionManager::adjustInternalStateForAction(
    const scxt::selection::SelectionManager::SelectActionContents &z)
{
#if DEBUG_SELECTION
    SCFCOUT << z << std::endl;
#endif
    auto za = (ZoneAddress)z;
    // OK so basically theres a few cases
    if (!z.selecting)
    {
        allSelectedZones.erase(za);
    }
    else
    {
        // So we are selecting
        if (z.distinct)
        {
            allSelectedZones.clear();
            allSelectedZones.insert(za);
            leadZone = za;
        }
        else
        {
            allSelectedZones.insert(za);
            if (z.selectingAsLead)
                leadZone = za;
        }
    }
}

void SelectionManager::guaranteeSelectedLead()
{
    // Now at the end of this the lead zone could not be selected
    if (allSelectedZones.find(leadZone) == allSelectedZones.end())
    {
        if (allSelectedZones.empty())
        {
            // Oh what to do here. Well reject it.
#if DEBUG_SELECTION
            SCFCOUT << "Be careful - we are promoting " << SCD(leadZone) << std::endl;
#endif
            allSelectedZones.insert(leadZone);
        }
        else
        {
            leadZone = *(allSelectedZones.begin());
        }
    }
}

void SelectionManager::debugDumpSelectionState()
{
#if DEBUG_SELECTION
    SCDBGCOUT << "---------------------------" << std::endl;
    SCDBGCOUT << SCD(leadZone) << std::endl;
    SCDBGCOUT << "All Selected Zones" << std::endl;
    for (const auto &s : allSelectedZones)
        SCDBGCOUT << "    - " << s << std::endl;
    SCDBGCOUT << "---------------------------" << std::endl;
#endif
}

void SelectionManager::sendSelectedZonesToClient()
{
#if DEBUG_SELECTION
    SCFCOUT << "Sending Data" << std::endl;
#endif
    serializationSendToClient(cms::s2c_send_selection_state,
                              cms::selectedStateMessage_t{leadZone, allSelectedZones},
                              *(engine.getMessageController()));
}

bool SelectionManager::ZoneAddress::isIn(const engine::Engine &e)
{
    if (part < 0 || part > engine::Patch::numParts)
        return false;
    const auto &p = e.getPatch()->getPart(part);
    if (group < 0 || group >= p->getGroups().size())
        return false;
    const auto &g = p->getGroup(group);
    if (zone < 0 || zone >= g->getZones().size())
        return false;
    return true;
}
} // namespace scxt::selection