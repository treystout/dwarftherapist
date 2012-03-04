#!/bin/sh
QT_DIR=~/QtSDK/Desktop/Qt/4.8.0/gcc
APP_DIR=/Volumes/Dwarf\ Therapist/DwarfTherapist.app

rm -rf ../../dist
mkdir -p ../../dist
hdiutil convert template.dmg -format UDSP -o ../../dist/DwarfTherapist
hdiutil resize -size 80m ../../dist/DwarfTherapist.sparseimage
hdiutil mount ../../dist/DwarfTherapist.sparseimage

# Prepare contents
cp -R ../../therapist-build/bin/release/DwarfTherapist.app/Contents /Volumes/Dwarf\ Therapist/DwarfTherapist.app/Contents
cp -f ../CHANGELOG.txt /Volumes/Dwarf\ Therapist/
cp -f ../LICENSE.txt /Volumes/Dwarf\ Therapist/
cp -f ../README.txt /Volumes/Dwarf\ Therapist/
rm -f /Volumes/Dwarf\ Therapist/DwarfTherapist.app/Contents/MacOS/log/run.log

macdeployqt "$APP_DIR" -no-plugins

#mkdir -p "$APP_DIR/Contents/Frameworks"
#cp -R $QT_DIR/lib/QtCore.framework "$APP_DIR/Contents/Frameworks"
#rm -rf "$APP_DIR/Contents/Frameworks/QtCore.framework/Versions/4/QtCore_debug"
#cp -R $QT_DIR/lib/QtScript.framework "$APP_DIR/Contents/Frameworks"
#rm -rf "$APP_DIR/Contents/Frameworks/QtScript.framework/Versions/4/QtScript_debug"
#cp -R $QT_DIR/lib/QtNetwork.framework "$APP_DIR/Contents/Frameworks"
#rm -rf "$APP_DIR/Contents/Frameworks/QtNetwork.framework/Versions/4/QtNetwork_debug"
#cp -R $QT_DIR/lib/QtGui.framework "$APP_DIR/Contents/Frameworks"
#rm -rf "$APP_DIR/Contents/Frameworks/QtGui.framework/Versions/4/QtGui_debug"

#install_name_tool -id @executable_path/../Frameworks/QtCore.framework/Versions/4/QtCore "$APP_DIR/Contents/Frameworks/QtCore.framework/Versions/4/QtCore"
#install_name_tool -id @executable_path/../Frameworks/QtScript.framework/Versions/4/QtScript "$APP_DIR/Contents/Frameworks/QtScript.framework/Versions/4/QtScript"
#install_name_tool -id @executable_path/../Frameworks/QtNetwork.framework/Versions/4/QtNetwork "$APP_DIR/Contents/Frameworks/QtNetwork.framework/Versions/4/QtNetwork"
#install_name_tool -id @executable_path/../Frameworks/QtGui.framework/Versions/4/QtGui "$APP_DIR/Contents/Frameworks/QtGui.framework/Versions/4/QtGui"

#install_name_tool -change $QT_DIR/lib/QtCore.framework/Versions/4/QtCore @executable_path/../Frameworks/QtCore.framework/Versions/4/QtCore "$APP_DIR/Contents/MacOS/DwarfTherapist"
#install_name_tool -change $QT_DIR/lib/QtScript.framework/Versions/4/QtScript @executable_path/../Frameworks/QtScript.framework/Versions/4/QtScript "$APP_DIR/Contents/MacOS/DwarfTherapist"
#install_name_tool -change $QT_DIR/lib/QtNetwork.framework/Versions/4/QtNetwork @executable_path/../Frameworks/QtNetwork.framework/Versions/4/QtNetwork "$APP_DIR/Contents/MacOS/DwarfTherapist"
#install_name_tool -change $QT_DIR/lib/QtGui.framework/Versions/4/QtGui @executable_path/../Frameworks/QtGui.framework/Versions/4/QtGui "$APP_DIR/Contents/MacOS/DwarfTherapist"

hdiutil eject /volumes/Dwarf\ Therapist/
hdiutil convert ../../dist/DwarfTherapist.sparseimage -format UDBZ -o ../../dist/DwarfTherapist.dmg
rm -f ../../dist/DwarfTherapist.sparseimage

