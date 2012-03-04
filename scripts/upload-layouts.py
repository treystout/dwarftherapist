#!/usr/bin/python
import re, sys, glob, os, os.path, errno, shutil, ftplib

CHECKSUM_RE = re.compile(r'checksum\s*=\s*([\w]+)')
PROP_RE = re.compile(r'(.*)=(.*)')

BASEDIR='memory_layouts'

def getProps(file):
	creds = {}
	
	path = os.path.expanduser(file)
	
	f = open(path, 'r')
	lines = f.readlines()
	f.close()
	
	for l in lines:
		m = PROP_RE.match(l)
		if m is not None:
			creds[m.group(1).strip()] = m.group(2).strip()
			
	return creds

def mkdir_p(ftp, path):
	try:
		ftp.mkd(path)
	except ftplib.error_perm as exc: # Python >2.5
		if str(exc).lower().find('file exists') != -1:
			pass
		else: raise

def getChecksum(path):
	f = open(path, 'r')
	lines = f.readlines()
	f.close();
	
	for line in lines:
		m = CHECKSUM_RE.match(line)
		if m is not None:
			return m.group(1)
	return None

def uploadLayout(ftp, path):	
	checksum = getChecksum(path)
	dir, name = os.path.split(path)
	subdir = os.path.split(dir)[1]
	
	f = open(path)
	ftp.storbinary('STOR ' + BASEDIR + '/' + subdir + '/' + name, f)
	f.seek(0)
	ftp.storbinary('STOR ' + BASEDIR + '/checksum/' + checksum, f)
	f.close()
	
	
if __name__ == "__main__":
	if len(sys.argv) != 2:
		print 'Specify arguments'
		exit(-1)
	
	props = getProps(sys.argv[1])
	
	ftp = ftplib.FTP(props['server'])
	ftp.login(props['user'], props['password'])
	
	ftp.cwd(props['remoteDir'])
	mkdir_p(ftp, BASEDIR)
	mkdir_p(ftp, BASEDIR + '/windows')
	mkdir_p(ftp, BASEDIR + '/linux')
	mkdir_p(ftp, BASEDIR + '/osx')
	mkdir_p(ftp, BASEDIR + '/checksum')
		
	for layout in glob.glob('etc/memory_layouts/*/*'):
		uploadLayout(ftp, layout)
		
	ftp.quit()

