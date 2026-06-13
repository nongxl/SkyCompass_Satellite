import datetime
import calendar

dt = datetime.datetime(2024, 6, 14, 0, 0, 0)
unix_ts = calendar.timegm(dt.utctimetuple())
print("2024-06-14 00:00:00 UTC =", unix_ts)
