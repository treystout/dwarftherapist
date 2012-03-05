#!/usr/bin/python
import re, sys, glob, os, os.path, errno, shutil, ftplib

PROP_RE = re.compile(r'(.*)=(.*)')

BASEDIR='apt'

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
	
if __name__ == "__main__":
	if len(sys.argv) != 3:
		print 'Specify arguments'
		exit(-1)
	
	props = getProps(sys.argv[1])
	localDir = sys.argv[2]
	
	ftp = ftplib.FTP(props['server'])
	ftp.login(props['user'], props['password'])
	
	ftp.cwd(props['remoteDir'])
	mkdir_p(ftp, BASEDIR)

	for path, dirs, filenames in os.walk(localDir):
		for d in dirs:
			remotepath = os.path.relpath(path + '/' + d, localDir)
			print 'MKDIR ' + BASEDIR + '/' + remotepath
			mkdir_p(ftp, BASEDIR + '/' + remotepath)
		for f in filenames:
			remotepath = os.path.relpath(path + '/' + f, localDir)
			fin = open(path + '/' + f)
			print 'STOR ' + path + '/' + f + ' -> ' + BASEDIR + '/' + remotepath
			ftp.storbinary('STOR ' + BASEDIR + '/' + remotepath, fin)
			fin.close()
		
	ftp.quit()

