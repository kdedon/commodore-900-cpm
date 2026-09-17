CP/M-8000 drive A: for the Commodore 900.

Stock DRI utilities: STAT DUMP DDT ED PIP, and the native development
pack -- ZCC (with ZCC1/2/3), ASZ8K, LD8K, AR8K, NMZ8K, SIZEZ8K, the C
headers and LIBCPM.A.  HELLO.C compiles and runs on this machine:

	A>ZCC HELLO.C
	A>HELLO

Utilities added by this port: SDIR SHOW SUBMIT SET INITDIR GENCOM
RSXLDR DATE HELP PAGET GET PUT.  Type a name with no arguments for its
usage.

	A>HELP

lists the topics it holds and HELP followed by a topic name prints it;
it reads HELP.HLP, so keep the two together.  PAGET turns the console's
page mode on and off.  GET takes console input for a program from a
file, and PUT captures console output to one; each attaches a resident
module, GET.RSX or PUT.RSX, which must be on the disk beside it.

Application software, from C in the CP/M-68K users' group collection:
SDB, David Betz's relational database (type `help' at its SDB> prompt;
it reads SDB.HLP), and Robert Heller's SORTFL, KILLDU, TOHEX and FROMHEX.
Their source is on the development disk, and ZCC builds it here.  Like
every command here they set a return code; see the SUBMIT topic in HELP
for IF ERROR.

SET supports file passwords and READ, WRITE or DELETE protection through
XFCBs. Enable enforcement with SET [PROTECT=ON] on the drive, and supply
a default file password with SET [DEFAULT=...]. SDIR [XFCB] and [NONXFCB]
filter files by whether they have an extended FCB.
