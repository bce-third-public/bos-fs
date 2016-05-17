import sys
import time
import datetime
from dateutil import tz
def ParseFlagFile(file):
    items = file.split('#')
    if (len(items)) != 4 and len(items) != 1:
        sys.exit(-1)

    if len(items) == 1:
        return items[0], None, None

    return items[0], items[1], items[3]

def IsFlagFile(file):
    file_name, _, _ = ParseFlagFile(file)
    return file_name == file

def BceTime2Timestamp(bcetime):
    format = "%Y-%m-%dT%H:%M:%SZ"
    utc_datetime = datetime.datetime.strptime(bcetime, format).replace(tzinfo=tz.tzutc()).astimezone(tz.tzlocal())
    return int(time.mktime(utc_datetime.timetuple()))

print BceTime2Timestamp("2015-04-27T08:23:49Z")