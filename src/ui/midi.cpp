// MIDI input through the ALSA sequencer (the original used the winmm midiIn API).
#include "mainwindow.h"

#ifdef VGMMM_HAVE_ALSA
#include <alsa/asoundlib.h>
#include <atomic>
#include <thread>

namespace {
struct Port {
    int client, port;
    QString name;
};
snd_seq_t *s_seq;
int s_inPort = -1;
std::thread s_thread;
std::atomic<bool> s_stop;
std::function<void(unsigned)> s_handler;

QList<Port> list_ports()
{
    QList<Port> out;
    snd_seq_t *seq;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0)
        return out;
    snd_seq_client_info_t *ci;
    snd_seq_port_info_t *pi;
    snd_seq_client_info_alloca(&ci);
    snd_seq_port_info_alloca(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        int client = snd_seq_client_info_get_client(ci);
        if (client == SND_SEQ_CLIENT_SYSTEM || client == snd_seq_client_id(seq))
            continue;
        snd_seq_port_info_set_client(pi, client);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pi);
            if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) !=
                (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
                continue;
            out.append({client, snd_seq_port_info_get_port(pi),
                        QString("%1: %2").arg(snd_seq_client_info_get_name(ci), snd_seq_port_info_get_name(pi))});
        }
    }
    snd_seq_close(seq);
    return out;
}

void reader()
{
    int n = snd_seq_poll_descriptors_count(s_seq, POLLIN);
    QVector<pollfd> fds(n);
    snd_seq_poll_descriptors(s_seq, fds.data(), (unsigned)n, POLLIN);
    while (!s_stop) {
        if (poll(fds.data(), (nfds_t)n, 100) <= 0)
            continue;
        snd_seq_event_t *ev;
        while (snd_seq_event_input_pending(s_seq, 1) > 0 && snd_seq_event_input(s_seq, &ev) >= 0) {
            unsigned msg = 0;
            switch (ev->type) {
            case SND_SEQ_EVENT_NOTEON:
                msg = 0x90u | (unsigned)ev->data.note.note << 8 | (unsigned)ev->data.note.velocity << 16;
                break;
            case SND_SEQ_EVENT_NOTEOFF:
                msg = 0x80u | (unsigned)ev->data.note.note << 8;
                break;
            case SND_SEQ_EVENT_PGMCHANGE:
                msg = 0xc0u | ((unsigned)ev->data.control.value & 0x7f) << 8;
                break;
            default:
                break;
            }
            if (msg != 0 && s_handler)
                s_handler(msg);
        }
    }
}
} // namespace

QStringList midi_list_inputs(void)
{
    QStringList names;
    for (const Port &p : list_ports())
        names << p.name;
    return names;
}

bool midi_open(int index, std::function<void(unsigned)> handler)
{
    midi_close();
    QList<Port> ports = list_ports();
    if (index < 0 || index >= ports.size())
        return false;
    if (snd_seq_open(&s_seq, "default", SND_SEQ_OPEN_INPUT, SND_SEQ_NONBLOCK) < 0) {
        s_seq = nullptr;
        return false;
    }
    snd_seq_set_client_name(s_seq, "VGM Music Maker");
    s_inPort = snd_seq_create_simple_port(s_seq, "MIDI in", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                          SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (s_inPort < 0 || snd_seq_connect_from(s_seq, s_inPort, ports[index].client, ports[index].port) < 0) {
        snd_seq_close(s_seq);
        s_seq = nullptr;
        return false;
    }
    s_handler = std::move(handler);
    s_stop = false;
    s_thread = std::thread(reader);
    return true;
}

void midi_close(void)
{
    if (s_seq == nullptr)
        return;
    s_stop = true;
    if (s_thread.joinable())
        s_thread.join();
    snd_seq_close(s_seq);
    s_seq = nullptr;
    s_handler = nullptr;
}

#else

QStringList midi_list_inputs(void) { return QStringList(); }
bool midi_open(int, std::function<void(unsigned)>) { return false; }
void midi_close(void) {}

#endif
