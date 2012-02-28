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

#include "dfinstance.h"
#include "dfinstanceosx.h"
#include "defines.h"
#include "dwarf.h"
#include "utils.h"
#include "gamedatareader.h"
#include "memorylayout.h"
#include "cp437codec.h"
#include "memorysegment.h"
#include "truncatingfilelogger.h"
#include <stdio.h>
#include <mach/vm_map.h>
#include <mach/mach_traps.h>
#include <mach-o/dyld.h>

DFInstanceOSX::DFInstanceOSX(QObject* parent)
	: DFInstance(parent)	
{   
}

DFInstanceOSX::~DFInstanceOSX() {
}

QVector<uint> DFInstanceOSX::enumerate_vector(const uint &addr) {
    attach();
    QVector<uint> addrs;
    detach();
    return addrs;
}

uint DFInstanceOSX::calculate_checksum() {
    // ELF binaries don't seem to store a linker timestamp, so just MD5 the file.
    uint md5 = 0; // we're going to throw away a lot of this checksum we just need 4bytes worth
    return md5;
}

QString DFInstanceOSX::read_string(const uint &addr) {
	QString ret_val = "FOO";
	return ret_val;
}

int DFInstanceOSX::write_string(const uint &addr, const QString &str) {
    Q_UNUSED(addr);
    Q_UNUSED(str);
	return 0;
}

int DFInstanceOSX::write_int(const uint &addr, const int &val) {
    return 0;
}

bool DFInstanceOSX::attach() {
    return true;
}

bool DFInstanceOSX::detach() {
	return true;
}

int DFInstanceOSX::read_raw(const VIRTADDR &addr, int bytes, QByteArray &buffer) {
    return 0;
}

int DFInstanceOSX::write_raw(const VIRTADDR &addr, const int &bytes, void *buffer) {
    return 0;
}

bool DFInstanceOSX::find_running_copy(bool) {
    return false;
}

void DFInstanceOSX::map_virtual_memory() {
}

