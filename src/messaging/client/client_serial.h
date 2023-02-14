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

#ifndef SCXT_SRC_MESSAGING_CLIENT_CLIENT_SERIAL_H
#define SCXT_SRC_MESSAGING_CLIENT_CLIENT_SERIAL_H

#include "messaging/messaging.h"

namespace scxt::messaging::client
{
/**
 * These IDs are used inside a session only and are not streamed,
 * so add whatever you want as long as (1) you keep them contig
 * (so don't assign values) and (2) the num_ enum is the last one
 */
enum ClientToSerializationMessagesIds
{
    c2s_on_register,

    c2s_single_select_address,

    c2s_request_zone_adsr_view,
    c2s_update_zone_adsr_view,

    c2s_request_pgz_structure,

    num_clientToSerializationMessages
};

enum SerializationToClientMessageIds
{
    s2c_voice_count,
    s2c_respond_zone_adsr_view,
    s2c_send_pgz_structure,

    num_serializationToClientMessages
};

typedef uint8_t unimpl_t;
template <ClientToSerializationMessagesIds id> struct ClientToSerializationType
{
    typedef unimpl_t T;
};

template <SerializationToClientMessageIds id> struct SerializationToClientType
{
    typedef unimpl_t T;
};

template <typename T> void clientSendToSerialization(const T &message, MessageController &mc);
template <typename T>
void serializationSendToClient(SerializationToClientMessageIds id, const T &payload,
                               messaging::MessageController &mc);

void serializationThreadExecuteClientMessage(const std::string &msgView, const engine::Engine &e,
                                             MessageController &mc);
template <typename Client>
void clientThreadExecuteSerializationMessage(const std::string &msgView, Client *c);

} // namespace scxt::messaging::client

#endif // SHORTCIRCUIT_CLIENT_SERIAL_H
