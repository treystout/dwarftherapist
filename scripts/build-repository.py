#!/usr/bin/python
import re, sys, shutil, os, os.path, subprocess, hashlib
from datetime import datetime

VERSIONS = { 'oneiric': '11.10', 'lucid': '10.04' }
DATE_FORMAT = '%a, %d %b %Y %H:%M:%S UTC'

def collectFiles(directory):
	files = set()
	for path, dirs, filenames in os.walk(directory):
		for f in filenames:
			files.add(os.path.relpath(path + '/' + f, directory))
	return files

def appendMD5Sums(fout, files):
	fout.write('MD5Sum:\n')
	for f in files:
		if f == 'Release':
			continue
		md5 = hashlib.md5(file(f, 'r').read()).hexdigest()
		size = str(os.path.getsize(f)).rjust(15)
		fout.write(' ' + md5 + '\t' + size + ' ' + f + '\n')

def appendSHA1Sums(fout, files):
	fout.write('SHA1:\n')
	for f in files:
		if f == 'Release':
			continue
		sha1 = hashlib.sha1(file(f, 'r').read()).hexdigest()
		size = str(os.path.getsize(f)).rjust(15)
		fout.write(' ' + sha1 + '\t' + size + ' ' + f + '\n')

def appendSHA256Sums(fout, files):
	fout.write('SHA256:\n')
	for f in files:
		if f == 'Release':
			continue
		sha256 = hashlib.sha256(file(f, 'r').read()).hexdigest()
		size = str(os.path.getsize(f)).rjust(15)
		fout.write(' ' + sha256 + '\t' + size + ' ' + f + '\n')
		
	
def buildArch(distdir, dist, arch, archname):
	os.chdir(distdir)
	print 'Scanning packages in:', distdir
	prefix = os.path.relpath(distdir, sys.argv[1]) + '/'
	subprocess.call('dpkg-scanpackages ' + arch + ' /dev/null ' + prefix + ' | gzip -9c > ' + arch + '/Packages.gz', shell=True)
	f = open(arch + '/Release', 'w')
	f.write('Archive: ' + dist + '\n')
	f.write('Version: ' + VERSIONS[dist] + '\n')
	f.write('Component: universe\n')
	f.write('Origin: DwarfTherapist\n')
	f.write('Label: DwarfTherapist\n')
	f.write('Architecture: ' + archname + '\n')
	f.close()

def buildDist(distsdir, dist):
	distdir = distsdir + '/' + dist + '/universe'
	arches = []
	for arch in os.listdir(distdir):
		archname = arch.split('-')[1]
		buildArch(distdir, dist, arch, archname)
		arches.append(archname)
	archnames = ''
	for name in arches:
		archnames = archnames + ' ' + name
	archnames = archnames[1:]

	files = collectFiles(distsdir + '/' + dist)

	os.chdir(distsdir + '/' + dist)
	f = open(distsdir + '/' + dist + '/Release', 'w')
	f.write('Origin: DwarfTherapist\n')
	f.write('Label: DwarfTherapist\n')
	f.write('Suite: ' + dist + '\n')
	f.write('Version: ' + VERSIONS[dist] + '\n')
	f.write('Codename: ' + dist + '\n')
	f.write('Date: ' + datetime.utcnow().strftime(DATE_FORMAT) + '\n')
	f.write('Architectures: ' + archnames + '\n')
	f.write('Components: universe\n')
	appendMD5Sums(f, files)
	f.write('\n')
	appendSHA1Sums(f, files)
	f.write('\n')
	appendSHA256Sums(f, files)
	f.close()

def buildRepository(basedir):
	distsdir = basedir + 'dists'
	for dist in os.listdir(distsdir):
		if dist != 'template':
			buildDist(distsdir, dist)

if __name__ == "__main__":
	if len(sys.argv) != 2:
		print "Specify repository directory"
		exit(-1)
	buildRepository(sys.argv[1])
