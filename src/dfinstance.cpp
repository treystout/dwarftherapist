/*
Dwarf Therapist
Copyright (c) 2009 Trey Stout (chmod)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <QtGui>
#include <QtDebug>
#include "defines.h"
#include "dfinstance.h"
#include "dwarf.h"
#include "squad.h"
#include "word.h"
#include "utils.h"
#include "gamedatareader.h"
#include "memorylayout.h"
#include "cp437codec.h"
#include "dwarftherapist.h"
#include "memorysegment.h"
#include "truncatingfilelogger.h"
#include "mainwindow.h"

#ifdef Q_WS_WIN
#define LAYOUT_SUBDIR "windows"
#include "dfinstancewindows.h"
#else
#ifdef Q_WS_X11
#define LAYOUT_SUBDIR "linux"
#include "dfinstancelinux.h"
#else
#ifdef Q_WS_MAC
#define LAYOUT_SUBDIR "osx"
#include "dfinstanceosx.h"
#endif
#endif
#endif

DFInstance::DFInstance(QObject* parent)
    : QObject(parent)
    , m_pid(0)
    , m_memory_correction(0)
    , m_stop_scan(false)
    , m_is_ok(true)
    , m_bytes_scanned(0)
    , m_layout(0)
    , m_attach_count(0)
    , m_heartbeat_timer(new QTimer(this))
    , m_memory_remap_timer(new QTimer(this))
    , m_scan_speed_timer(new QTimer(this))
    , m_dwarf_race_id(0)
{
    connect(m_scan_speed_timer, SIGNAL(timeout()),
            SLOT(calculate_scan_rate()));
    connect(m_memory_remap_timer, SIGNAL(timeout()),
            SLOT(map_virtual_memory()));
    m_memory_remap_timer->start(20000); // 20 seconds
    // let subclasses start the heartbeat timer, since we don't want to be
    // checking before we're connected
    connect(m_heartbeat_timer, SIGNAL(timeout()), SLOT(heartbeat()));

    // We need to scan for memory layout files to get a list of DF versions this
    // DT version can talk to. Start by building up a list of search paths
    QDir working_dir = QDir::current();
    QStringList search_paths;
    search_paths << working_dir.path();

    QString subdir = LAYOUT_SUBDIR;
    search_paths << QString("etc/memory_layouts/%1").arg(subdir);

    TRACE << "Searching for MemoryLayout ini files in the following directories";
    foreach(QString path, search_paths) {
        TRACE<< path;
        QDir d(path);
        d.setNameFilters(QStringList() << "*.ini");
        d.setFilter(QDir::NoDotAndDotDot | QDir::Readable | QDir::Files);
        d.setSorting(QDir::Name | QDir::Reversed);
        QFileInfoList files = d.entryInfoList();
        foreach(QFileInfo info, files) {
            MemoryLayout *temp = new MemoryLayout(info.absoluteFilePath());
            if (temp && temp->is_valid()) {
                LOGD << "adding valid layout" << temp->game_version()
                        << temp->checksum();
                m_memory_layouts.insert(temp->checksum().toLower(), temp);
            }
        }
    }
    // if no memory layouts were found that's a critical error
    if (m_memory_layouts.size() < 1) {
        LOGE << "No valid memory layouts found in the following directories..."
                << QDir::searchPaths("memory_layouts");
        qApp->exit(ERROR_NO_VALID_LAYOUTS);
    }
}

DFInstance::~DFInstance() {
    LOGD << "DFInstance baseclass virtual dtor!";
    foreach(MemoryLayout *l, m_memory_layouts) {
        delete(l);
    }
    m_memory_layouts.clear();
}

DFInstance * DFInstance::newInstance() {
#ifdef Q_WS_WIN
    return new DFInstanceWindows();
#else
#ifdef Q_WS_MAC
    return new DFInstanceOSX();
#else
#ifdef Q_WS_X11
    return new DFInstanceLinux();
#endif
#endif
#endif
}

BYTE DFInstance::read_byte(const VIRTADDR &addr) {
    QByteArray out;
    read_raw(addr, sizeof(BYTE), out);
    return out.at(0);
}

WORD DFInstance::read_word(const VIRTADDR &addr) {
    QByteArray out;
    read_raw(addr, sizeof(WORD), out);
    return decode_word(out);
}

VIRTADDR DFInstance::read_addr(const VIRTADDR &addr) {
    QByteArray out;
    read_raw(addr, sizeof(VIRTADDR), out);
    return decode_dword(out);
}

qint16 DFInstance::read_short(const VIRTADDR &addr) {
    QByteArray out;
    read_raw(addr, sizeof(qint16), out);
    return decode_short(out);
}

qint32 DFInstance::read_int(const VIRTADDR &addr) {
    QByteArray out;
    read_raw(addr, sizeof(qint32), out);
    return decode_int(out);
}

QVector<VIRTADDR> DFInstance::scan_mem(const QByteArray &needle, const uint start_addr, const uint end_addr) {
    // progress reporting
    m_scan_speed_timer->start(500);
    m_memory_remap_timer->stop(); // don't remap segments while scanning
    int total_bytes = 0;
    m_bytes_scanned = 0; // for global timings
    int bytes_scanned = 0; // for progress calcs
    foreach(MemorySegment *seg, m_regions) {
        total_bytes += seg->size;
    }
    int report_every_n_bytes = total_bytes / 1000;
    emit scan_total_steps(1000);
    emit scan_progress(0);


    m_stop_scan = false;
    QVector<VIRTADDR> addresses; //! return value
    QByteArrayMatcher matcher(needle);

    int step_size = 0x1000;
    QByteArray buffer(step_size, 0);
    QByteArray back_buffer(step_size * 2, 0);

    QTime timer;
    timer.start();
    attach();
    foreach(MemorySegment *seg, m_regions) {
        int step = step_size;
        int steps = seg->size / step;
        if (seg->size % step)
            steps++;

        if( seg->end_addr < start_addr ) {
            continue;
        }

        if( seg->start_addr > end_addr ) {
            break;
        }

        for(VIRTADDR ptr = seg->start_addr; ptr < seg->end_addr; ptr += step) {

            if( ptr < start_addr ) {
                continue;
            }
            if( ptr > end_addr ) {
                m_stop_scan = true;
                break;
            }

            step = step_size;
            if (ptr + step > seg->end_addr) {
                step = seg->end_addr - ptr;
            }

            // move the last thing we read to the front of the back_buffer
            back_buffer.replace(0, step, buffer);

            // fill the main read buffer
            int bytes_read = read_raw(ptr, step, buffer);
            if (bytes_read < step && !seg->is_guarded) {
                if (m_layout->is_complete()) {
                    LOGW << "tried to read" << step << "bytes starting at" <<
                            hexify(ptr) << "but only got" << dec << bytes_read;
                }
                continue;
            }
            bytes_scanned += bytes_read;
            m_bytes_scanned += bytes_read;

            // put the main buffer on the end of the back_buffer
            back_buffer.replace(step, step, buffer);

            int idx = -1;
            forever {
                idx = matcher.indexIn(back_buffer, idx+1);
                if (idx == -1) {
                    break;
                } else {
                    VIRTADDR hit = ptr + idx - step;
                    if (!addresses.contains(hit)) {
                        // backbuffer may cause duplicate hits
                        addresses << hit;
                    }
                }
            }

            if (m_stop_scan)
                break;
            emit scan_progress(bytes_scanned / report_every_n_bytes);

        }
        DT->processEvents();
        if (m_stop_scan)
            break;
    }
    detach();
    m_memory_remap_timer->start(20000); // start the remapper again
    LOGD << QString("Scanned %L1MB in %L2ms").arg(bytes_scanned / 1024 * 1024)
            .arg(timer.elapsed());
    return addresses;
}

bool DFInstance::looks_like_vector_of_pointers(const VIRTADDR &addr) {
    int start = read_int(addr + 0x4);
    int end = read_int(addr + 0x8);
    int entries = (end - start) / sizeof(int);
    LOGD << "LOOKS LIKE VECTOR? unverified entries:" << entries;

    return start >=0 &&
           end >=0 &&
           end >= start &&
           (end-start) % 4 == 0 &&
           start % 4 == 0 &&
           end % 4 == 0 &&
           entries < 10000;

}

void DFInstance::read_raws() {
    emit progress_message(tr("Reading raws"));

    LOGI << "Reading some game raws...";
    GameDataReader::ptr()->read_raws(m_df_dir);
}

QVector<Dwarf*> DFInstance::load_dwarves() {
    map_virtual_memory();
    QVector<Dwarf*> dwarves;
    if (!m_is_ok) {
        LOGW << "not connected";
        detach();
        return dwarves;
    }

    // we're connected, make sure we have good addresses
    VIRTADDR creature_vector = m_layout->address("creature_vector");
    creature_vector += m_memory_correction;
    VIRTADDR dwarf_race_index = m_layout->address("dwarf_race_index");
    dwarf_race_index += m_memory_correction;

    if (!is_valid_address(creature_vector)) {
        LOGW << "Active Memory Layout" << m_layout->filename() << "("
                << m_layout->game_version() << ")" << "contains an invalid"
                << "creature_vector address. Either you are scanning a new "
                << "DF version or your config files are corrupted.";
        return dwarves;
    }
    if (!is_valid_address(dwarf_race_index)) {
        LOGW << "Active Memory Layout" << m_layout->filename() << "("
                << m_layout->game_version() << ")" << "contains an invalid"
                << "dwarf_race_index address. Either you are scanning a new "
                << "DF version or your config files are corrupted.";
        return dwarves;
    }

    // both necessary addresses are valid, so let's try to read the creatures
    LOGD << "loading creatures from " << hexify(creature_vector) <<
            hexify(creature_vector - m_memory_correction) << "(UNCORRECTED)";
    LOGD << "dwarf race index" << hexify(dwarf_race_index) <<
            hexify(dwarf_race_index - m_memory_correction) << "(UNCORRECTED)";
    emit progress_message(tr("Loading Dwarves"));

    attach();
    // which race id is dwarven?
    m_dwarf_race_id = read_word(dwarf_race_index);
    LOGD << "dwarf race:" << hexify(m_dwarf_race_id);

    QVector<VIRTADDR> entries = enumerate_vector(creature_vector);
    emit progress_range(0, entries.size()-1);
    TRACE << "FOUND" << entries.size() << "creatures";
    if (!entries.empty()) {
        Dwarf *d = 0;
        int i = 0;
        foreach(VIRTADDR creature_addr, entries) {
            d = Dwarf::get_dwarf(this, creature_addr);
            if (d) {
                dwarves.append(d);
                LOGD << "FOUND DWARF" << hexify(creature_addr)
                     << d->nice_name();
            } else {
                TRACE << "FOUND OTHER CREATURE" << hexify(creature_addr);
            }
            emit progress_value(i++);
        }
    } else {
        // we lost the fort!
        m_is_ok = false;
    }
    detach();
    LOGI << "found" << dwarves.size() << "dwarves out of" << entries.size()
            << "creatures";
    return dwarves;
}

QVector<Squad*> DFInstance::load_squads() {

    QVector<Squad*> squads;
    if (!m_is_ok) {
        LOGW << "not connected";
        detach();
        return squads;
    }

    // we're connected, make sure we have good addresses
    VIRTADDR squad_vector = m_layout->address("squad_vector");
    if(squad_vector == 0xFFFFFFFF) {
        LOGI << "Squads not supported for this version of Dwarf Fortress";
        return squads;
    }

    squad_vector += m_memory_correction;

    if (!is_valid_address(squad_vector)) {
        LOGW << "Active Memory Layout" << m_layout->filename() << "("
                << m_layout->game_version() << ")" << "contains an invalid"
                << "squad_vector address. Either you are scanning a new "
                << "DF version or your config files are corrupted.";
        return squads;
    }

    // both necessary addresses are valid, so let's try to read the creatures
    LOGD << "loading squads from " << hexify(squad_vector) <<
            hexify(squad_vector - m_memory_correction) << "(UNCORRECTED)";

    emit progress_message(tr("Loading Squads"));

    attach();

    QVector<VIRTADDR> entries = enumerate_vector(squad_vector);
    TRACE << "FOUND" << entries.size() << "squads";

    if (!entries.empty()) {
        emit progress_range(0, entries.size()-1);
        Squad *s = NULL;
        int i = 0;
        foreach(VIRTADDR squad_addr, entries) {
            s = Squad::get_squad(this, squad_addr);
            if (s) {
                TRACE << "FOUND SQUAD" << hexify(squad_addr) << s->name();
                squads << s;
            }
            emit progress_value(i++);
        }
    }

    detach();
    LOGI << "Found" << squads.size() << "squads out of" << entries.size();
    return squads;
}

void DFInstance::heartbeat() {
    // simple read attempt that will fail if the DF game isn't running a fort,
    // or isn't running at all
    QVector<VIRTADDR> creatures = enumerate_vector(
            m_layout->address("creature_vector") + m_memory_correction);
    if (creatures.size() < 1) {
        // no game loaded, or process is gone
        emit connection_interrupted();
    }
}

bool DFInstance::is_valid_address(const VIRTADDR &addr) {
    bool valid = false;
    foreach(MemorySegment *seg, m_regions) {
        if (seg->contains(addr)) {
            valid = true;
            break;
        }
    }
    return valid;
}

QByteArray DFInstance::get_data(const VIRTADDR &addr, int size) {
    QByteArray ret_val(size, 0); // 0 filled to proper length
    int bytes_read = read_raw(addr, size, ret_val);
    if (bytes_read != size) {
        ret_val.clear();
    }
    return ret_val;
}

//! ahhh convenience
QString DFInstance::pprint(const VIRTADDR &addr, int size) {
    return pprint(get_data(addr, size), addr);
}

QString DFInstance::pprint(const QByteArray &ba, const VIRTADDR &start_addr) {
    QString out = "    ADDR   | 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F | TEXT\n";
    out.append("------------------------------------------------------------------------\n");
    int lines = ba.size() / 16;
    if (ba.size() % 16)
        lines++;
    if (lines < 1)
        lines = 0;

    for(int i = 0; i < lines; ++i) {
        VIRTADDR offset = start_addr + i * 16;
        out.append(hexify(offset));
        out.append(" | ");
        for (int c = 0; c < 16; ++c) {
            out.append(ba.mid(i*16 + c, 1).toHex());
            out.append(" ");
        }
        out.append("| ");
        for (int c = 0; c < 16; ++c) {
            QByteArray tmp = ba.mid(i*16 + c, 1);
            if (tmp.at(0) == 0)
                out.append(".");
            else if (tmp.at(0) <= 126 && tmp.at(0) >= 32)
                out.append(tmp);
            else
                out.append(tmp.toHex());
        }
        //out.append(ba.mid(i*16, 16).toPercentEncoding());
        out.append("\n");
    }
    return out;
}

Word * DFInstance::read_dwarf_word(const VIRTADDR &addr) {
    Word * result = NULL;
    uint word_id = read_int(addr);
    if(word_id != 0xFFFFFFFF) {
        result = DT->get_word(word_id);
    }
    return result;
}

QString DFInstance::read_dwarf_name(const VIRTADDR &addr) {
    QString result = "The";

    //7 parts e.g.  ffffffff ffffffff 000006d4
    //      ffffffff ffffffff 000002b1 ffffffff

    //Unknown
    Word * word = read_dwarf_word(addr);
    if(word)
        result.append(" " + capitalize(word->base()));

    //Unknown
    word = read_dwarf_word(addr + 0x04);
    if(word)
        result.append(" " + capitalize(word->base()));

    //Verb
    word = read_dwarf_word(addr + 0x08);
    if(word) {
        result.append(" " + capitalize(word->adjective()));
    }

    //Unknown
    word = read_dwarf_word(addr + 0x0C);
    if(word)
        result.append(" " + capitalize(word->base()));

    //Unknown
    word = read_dwarf_word(addr + 0x10);
    if(word)
        result.append(" " + capitalize(word->base()));

    //Noun
    word = read_dwarf_word(addr + 0x14);
    bool singular = false;
    if(word) {
        if(word->plural_noun().isEmpty()) {
            result.append(" " + capitalize(word->noun()));
            singular = true;
        } else {
            result.append(" " + capitalize(word->plural_noun()));
        }
    }

    //of verb(noun)
    word = read_dwarf_word(addr + 0x18);
    if(word) {
        if( !word->verb().isEmpty() ) {
            if(singular) {
                result.append(" of " + capitalize(word->verb()));
            } else {
                result.append(" of " + capitalize(word->present_participle_verb()));
            }
        } else {
            if(singular) {
                result.append(" of " + capitalize(word->noun()));
            } else {
                result.append(" of " + capitalize(word->plural_noun()));
            }
        }
    }

    return result.trimmed();
}


QVector<VIRTADDR> DFInstance::find_vectors_in_range(const int &max_entries,
                                                const VIRTADDR &start_address,
                                                const int &range_length) {
    QByteArray data = get_data(start_address, range_length);
    QVector<VIRTADDR> vectors;
    VIRTADDR int1 = 0; // holds the start val
    VIRTADDR int2 = 0; // holds the end val

    for (int i = 0; i < range_length; i += 4) {
        memcpy(&int1, data.data() + i, 4);
        memcpy(&int2, data.data() + i + 4, 4);
        if (int2 >= int1 && is_valid_address(int1) && is_valid_address(int2)) {
            int bytes = int2 - int1;
            int entries = bytes / 4;
            if (entries > 0 && entries <= max_entries) {
                VIRTADDR vector_addr = start_address + i - VECTOR_POINTER_OFFSET;
                QVector<VIRTADDR> addrs = enumerate_vector(vector_addr);
                bool all_valid = true;
                foreach(VIRTADDR vec_entry, addrs) {
                    if (!is_valid_address(vec_entry)) {
                        all_valid = false;
                        break;
                    }
                }
                if (all_valid) {
                    vectors << vector_addr;
                }
            }
        }
    }
    return vectors;
}

QVector<VIRTADDR> DFInstance::find_vectors(int num_entries, int fuzz/* =0 */,
                                           int entry_size/* =4 */) {
    /*
    glibc++ does vectors like so...
    |4bytes      | 4bytes    | 4bytes
    START_ADDRESS|END_ADDRESS|END_ALLOCATOR

    MSVC++ does vectors like so...
    | 4bytes     | 4bytes      | 4 bytes   | 4bytes
    ALLOCATOR    |START_ADDRESS|END_ADDRESS|END_ALLOCATOR
    */
    m_stop_scan = false; //! if ever set true, bail from the inner loop
    QVector<VIRTADDR> vectors; //! return value collection of vectors found
    VIRTADDR int1 = 0; // holds the start val
    VIRTADDR int2 = 0; // holds the end val

    // progress reporting
    m_scan_speed_timer->start(500);
    m_memory_remap_timer->stop(); // don't remap segments while scanning
    int total_bytes = 0;
    m_bytes_scanned = 0; // for global timings
    int bytes_scanned = 0; // for progress calcs
    foreach(MemorySegment *seg, m_regions) {
        total_bytes += seg->size;
    }
    int report_every_n_bytes = total_bytes / 1000;
    emit scan_total_steps(1000);
    emit scan_progress(0);

    int scan_step_size = 0x10000;
    QByteArray buffer(scan_step_size, '\0');
    QTime timer;
    timer.start();
    attach();
    foreach(MemorySegment *seg, m_regions) {
        //TRACE << "SCANNING REGION" << hex << seg->start_addr << "-"
        //        << seg->end_addr << "BYTES:" << dec << seg->size;
        if ((int)seg->size <= scan_step_size) {
            scan_step_size = seg->size;
        }
        int scan_steps = seg->size / scan_step_size;
        if (seg->size % scan_step_size) {
            scan_steps++;
        }
        VIRTADDR addr = 0; // the ptr we will read from
        for(int step = 0; step < scan_steps; ++step) {
            addr = seg->start_addr + (scan_step_size * step);
            //LOGD << "starting scan for vectors at" << hex << addr << "step"
            //        << dec << step << "of" << scan_steps;
            int bytes_read = read_raw(addr, scan_step_size, buffer);
            if (bytes_read < scan_step_size) {
                continue;
            }
            for(int offset = 0; offset < scan_step_size; offset += entry_size) {
                int1 = decode_int(buffer.mid(offset, entry_size));
                int2 = decode_int(buffer.mid(offset + entry_size, entry_size));
                if (int1 && int2 && int2 >= int1
                    && int1 % 4 == 0
                    && int2 % 4 == 0
                    //&& is_valid_address(int1)
                    //&& is_valid_address(int2)
                    ) {
                    int bytes = int2 - int1;
                    int entries = bytes / entry_size;
                    int diff = entries - num_entries;
                    if (qAbs(diff) <= fuzz) {
                        VIRTADDR vector_addr = addr + offset -
                                               VECTOR_POINTER_OFFSET;
                        QVector<VIRTADDR> addrs = enumerate_vector(vector_addr);
                        diff = addrs.size() - num_entries;
                        if (qAbs(diff) <= fuzz) {
                            vectors << vector_addr;
                        }
                    }
                }
                m_bytes_scanned += entry_size;
                bytes_scanned += entry_size;
                if (m_stop_scan)
                    break;
            }
            emit scan_progress(bytes_scanned / report_every_n_bytes);
            DT->processEvents();
            if (m_stop_scan)
                break;
        }
    }
    detach();
    m_memory_remap_timer->start(20000); // start the remapper again
    m_scan_speed_timer->stop();
    LOGD << QString("Scanned %L1MB in %L2ms").arg(bytes_scanned / 1024 * 1024)
            .arg(timer.elapsed());
    emit scan_progress(100);
    return vectors;
}

QVector<VIRTADDR> DFInstance::find_vectors_ext(int num_entries, const char op,
                              const uint start_addr, const uint end_addr, int entry_size/* =4 */) {
    /*
    glibc++ does vectors like so...
    |4bytes      | 4bytes    | 4bytes
    START_ADDRESS|END_ADDRESS|END_ALLOCATOR

    MSVC++ does vectors like so...
    | 4bytes     | 4bytes      | 4 bytes   | 4bytes
    ALLOCATOR    |START_ADDRESS|END_ADDRESS|END_ALLOCATOR
    */
    m_stop_scan = false; //! if ever set true, bail from the inner loop
    QVector<VIRTADDR> vectors; //! return value collection of vectors found
    VIRTADDR int1 = 0; // holds the start val
    VIRTADDR int2 = 0; // holds the end val

    // progress reporting
    m_scan_speed_timer->start(500);
    m_memory_remap_timer->stop(); // don't remap segments while scanning
    int total_bytes = 0;
    m_bytes_scanned = 0; // for global timings
    int bytes_scanned = 0; // for progress calcs
    foreach(MemorySegment *seg, m_regions) {
        total_bytes += seg->size;
    }
    int report_every_n_bytes = total_bytes / 1000;
    emit scan_total_steps(1000);
    emit scan_progress(0);

    int scan_step_size = 0x10000;
    QByteArray buffer(scan_step_size, '\0');
    QTime timer;
    timer.start();
    attach();
    foreach(MemorySegment *seg, m_regions) {
        //TRACE << "SCANNING REGION" << hex << seg->start_addr << "-"
        //        << seg->end_addr << "BYTES:" << dec << seg->size;
        if ((int)seg->size <= scan_step_size) {
            scan_step_size = seg->size;
        }
        int scan_steps = seg->size / scan_step_size;
        if (seg->size % scan_step_size) {
            scan_steps++;
        }

        if( seg->end_addr < start_addr ) {
            continue;
        }

        if( seg->start_addr > end_addr ) {
            break;
        }

        VIRTADDR addr = 0; // the ptr we will read from
        for(int step = 0; step < scan_steps; ++step) {
            addr = seg->start_addr + (scan_step_size * step);
            //LOGD << "starting scan for vectors at" << hex << addr << "step"
            //        << dec << step << "of" << scan_steps;
            int bytes_read = read_raw(addr, scan_step_size, buffer);
            if (bytes_read < scan_step_size) {
                continue;
            }

            for(int offset = 0; offset < scan_step_size; offset += entry_size) {
                VIRTADDR vector_addr = addr + offset - VECTOR_POINTER_OFFSET;

                if( vector_addr < start_addr ) {
                    continue;
                }

                if( vector_addr > end_addr ) {
                    m_stop_scan = true;
                    break;
                }

                int1 = decode_int(buffer.mid(offset, entry_size));
                int2 = decode_int(buffer.mid(offset + entry_size, entry_size));
                if (int1 && int2 && int2 >= int1
                    && int1 % 4 == 0
                    && int2 % 4 == 0
                    //&& is_valid_address(int1)
                    //&& is_valid_address(int2)
                    ) {
                    int bytes = int2 - int1;
                    int entries = bytes / entry_size;
                    if (entries > 0 && entries < 1000) {
                        QVector<VIRTADDR> addrs = enumerate_vector(vector_addr);
                        if( (op == '=' && addrs.size() == num_entries)
                                || (op == '<' && addrs.size() < num_entries)
                                || (op == '>' && addrs.size() > num_entries) ) {
                            vectors << vector_addr;
                        }
                    }
                }
                m_bytes_scanned += entry_size;
                bytes_scanned += entry_size;
                if (m_stop_scan)
                    break;
            }
            emit scan_progress(bytes_scanned / report_every_n_bytes);
            DT->processEvents();
            if (m_stop_scan)
                break;
        }
    }
    detach();
    m_memory_remap_timer->start(20000); // start the remapper again
    m_scan_speed_timer->stop();
    LOGD << QString("Scanned %L1MB in %L2ms").arg(bytes_scanned / 1024 * 1024)
            .arg(timer.elapsed());
    emit scan_progress(100);
    return vectors;
}

QVector<VIRTADDR> DFInstance::find_vectors(int num_entries, const QVector<VIRTADDR> & search_set,
                               int fuzz/* =0 */, int entry_size/* =4 */) {

    m_stop_scan = false; //! if ever set true, bail from the inner loop
    QVector<VIRTADDR> vectors; //! return value collection of vectors found

    // progress reporting
    m_scan_speed_timer->start(500);
    m_memory_remap_timer->stop(); // don't remap segments while scanning

    int total_vectors = vectors.size();
    m_bytes_scanned = 0; // for global timings
    int vectors_scanned = 0; // for progress calcs

    emit scan_total_steps(total_vectors);
    emit scan_progress(0);

    QTime timer;
    timer.start();
    attach();

    int vector_size = 8 + VECTOR_POINTER_OFFSET;
    QByteArray buffer(vector_size, '\0');

    foreach(VIRTADDR addr, search_set) {
        int bytes_read = read_raw(addr, vector_size, buffer);
        if (bytes_read < vector_size) {
            continue;
        }

        VIRTADDR int1 = 0; // holds the start val
        VIRTADDR int2 = 0; // holds the end val
        int1 = decode_int(buffer.mid(VECTOR_POINTER_OFFSET, sizeof(VIRTADDR)));
        int2 = decode_int(buffer.mid(VECTOR_POINTER_OFFSET+ sizeof(VIRTADDR), sizeof(VIRTADDR)));

        if (int1 && int2 && int2 >= int1
                && int1 % 4 == 0
                && int2 % 4 == 0) {

            int bytes = int2 - int1;
            int entries = bytes / entry_size;
            int diff = entries - num_entries;
            if (qAbs(diff) <= fuzz) {
                QVector<VIRTADDR> addrs = enumerate_vector(addr);
                diff = addrs.size() - num_entries;
                if (qAbs(diff) <= fuzz) {
                    vectors << addr;
                }
            }
        }

        vectors_scanned++;

        if(vectors_scanned % 100 == 0) {
            emit scan_progress(vectors_scanned);
            DT->processEvents();
        }

        if (m_stop_scan)
            break;
    }


    detach();
    m_memory_remap_timer->start(20000); // start the remapper again
    m_scan_speed_timer->stop();
    LOGD << QString("Scanned %L1 vectors in %L2ms").arg(vectors_scanned)
            .arg(timer.elapsed());
    emit scan_progress(100);
    return vectors;
}


MemoryLayout *DFInstance::get_memory_layout(QString checksum, bool) {
    checksum = checksum.toLower();
    LOGD << "DF's checksum is:" << checksum;

    MemoryLayout *ret_val = NULL;
    ret_val = m_memory_layouts.value(checksum, NULL);
    m_is_ok = ret_val != NULL && ret_val->is_valid();

    if(!m_is_ok) {
        LOGD << "Could not find layout for checksum" << checksum;
        DT->get_main_window()->check_for_layout(checksum);
    }

    if (m_is_ok) {
        LOGI << "Detected Dwarf Fortress version"
                << ret_val->game_version() << "using MemoryLayout from"
                << ret_val->filename();
    }

    return ret_val;
}

bool DFInstance::add_new_layout(const QString & version, QFile & file) {
    QString newFileName = version;
    newFileName.replace("(", "").replace(")", "").replace(" ", "_");
    newFileName +=  ".ini";

    QFileInfo newFile(QDir(QString("etc/memory_layouts/%1").arg(LAYOUT_SUBDIR)), newFileName);
    newFileName = newFile.absoluteFilePath();

    if(!file.exists()) {
        LOGW << "Layout file" << file.fileName() << "does not exist!";
        return false;
    }

    LOGD << "Copying: " << file.fileName() << " to " << newFileName;
    if(!file.copy(newFileName)) {
        LOGW << "Error renaming layout file!";
        return false;
    }

    MemoryLayout *temp = new MemoryLayout(newFileName);
    if (temp && temp->is_valid()) {
        LOGD << "adding valid layout" << temp->game_version() << temp->checksum();
        m_memory_layouts.insert(temp->checksum().toLower(), temp);
    }
    return true;
}

void DFInstance::layout_not_found(const QString & checksum) {
    QString supported_vers;

    // TODO: Replace this with a rich dialog at some point that
    // is also accessible from the help menu. For now, remove the
    // extra path information as the dialog is getting way too big.
    // And make a half-ass attempt at sorting
    QList<MemoryLayout *> layouts = m_memory_layouts.values();
    qSort(layouts);

    foreach(MemoryLayout * l, layouts) {
        supported_vers.append(
                QString("<li><b>%1</b>(<font color=\"#444444\">%2"
                        "</font>)</li>")
                .arg(l->game_version())
                .arg(l->checksum()));
    }

    QMessageBox *mb = new QMessageBox(qApp->activeWindow());
    mb->setIcon(QMessageBox::Critical);
    mb->setWindowTitle(tr("Unidentified Game Version"));
    mb->setText(tr("I'm sorry but I don't know how to talk to this "
        "version of Dwarf Fortress! (checksum:%1)<br><br> <b>Supported "
        "Versions:</b><ul>%2</ul>").arg(checksum).arg(supported_vers));
    mb->setInformativeText(tr("<a href=\"%1\">Click Here to find out "
                              "more online</a>.")
                           .arg(URL_SUPPORTED_GAME_VERSIONS));

    /*
    mb->setDetailedText(tr("Failed to locate a memory layout file for "
        "Dwarf Fortress exectutable with checksum '%1'").arg(checksum));
    */
    mb->exec();
    LOGE << tr("unable to identify version from checksum:") << checksum;
}

void DFInstance::calculate_scan_rate() {
    float rate = (m_bytes_scanned / 1024.0f / 1024.0f) /
                 (m_scan_speed_timer->interval() / 1000.0f);
    QString msg = QString("%L1MB/s").arg(rate);
    emit scan_message(msg);
    m_bytes_scanned = 0;
}
