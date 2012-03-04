#!/bin/sh
rm -rf ../../dist
mkdir -p ../../dist
hdiutil convert template.dmg -format UDSP -o ../../dist/DwarfTherapist
hdiutil mount ../../dist/DwarfTherapist.sparseimage
cp -R ../../therapist-build/bin/release/DwarfTherapist.app/Contents /Volumes/Dwarf\ Therapist/DwarfTherapist.app/Contents
cp -f ../CHANGELOG.txt /Volumes/Dwarf\ Therapist/
cp -f ../LICENSE.txt /Volumes/Dwarf\ Therapist/
cp -f ../README.txt /Volumes/Dwarf\ Therapist/
rm -f /Volumes/Dwarf\ Therapist/DwarfTherapist.app/Contents/MacOS/log/run.log
hdiutil eject /volumes/Dwarf\ Therapist/
hdiutil convert ../../dist/DwarfTherapist.sparseimage -format UDBZ -o ../../dist/DwarfTherapist.dmg
rm -f ../../dist/DwarfTherapist.sparseimage

