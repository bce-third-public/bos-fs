from bos_tool import GetBosClient
from config import *
from fs_tool import *
from collections import OrderedDict
import time
def BosFsck(bucket, root = ""):
    bos_client = GetBosClient(host, ak, sk)
    marker = ""
    files = OrderedDict()
    dirs = OrderedDict()
    while True:
        response = bos_client.list_objects(bucket, prefix=root, marker=marker, delimiter="/")
        print response
        for file in response.contents:
            if file.key.endswith("/"):
                continue

            files[file.key] = file
        for dir in response.common_prefixes:
            dirs[dir.prefix] = dir

        if response.is_truncated == False:
            break

        marker = response.nextMarker

    fs_file_attr = OrderedDict()

    for (dir, _) in dirs.items():
        filename = dir[0:len(dir) - 1]
        attr = dict()
        attr["last_modify"] = time.time()
        attr["mode"] = default_mode
        attr["lost_flag"] = True
        fs_file_attr[filename] = attr

        filename = dir[0:len(dir) - 1]
        # bos_client.get_object_as_string(bucket, filename, [0, 1])
        dest_user_meta = dict()
        dest_user_meta["fs-mode"] = "%d" % (default_mode)
        dest_user_meta["fs-file-type"] = "dir"
        bos_client.put_object_from_string(bucket, filename.encode("utf-8"), "", user_metadata=dest_user_meta)
        bos_client.put_object_from_string(bucket, dir.encode("utf-8"), "", user_metadata=dest_user_meta)

    for (object_name, file) in files.items():
        filename, last_modify, mode = ParseFlagFile(object_name)
        print filename
        if filename == object_name:
            attr = dict()
            attr["last_modify"] = BceTime2Timestamp(file.last_modified)
            attr["mode"] = default_mode
            attr["lost_flag"] = True
            fs_file_attr[filename] = attr
        else:
            if fs_file_attr.has_key(filename):
                if fs_file_attr[filename]["mode"] == default_mode:
                    fs_file_attr[filename]["mode"] == mode
                    fs_file_attr[filename]["lost_flag"] = False

    files.clear()

    for (filename, file_attr) in fs_file_attr.items():
        if file_attr["lost_flag"]:
            flag_file = '%s#%d#0#%d' % (filename, file_attr["mode"], attr["last_modify"])
            bos_client.put_object_from_string(bucket, flag_file.encode("utf-8"), "")

            dest_user_meta = dict()
            dest_user_meta["fs-mode"] = "%d" % (file_attr["mode"])
            dest_user_meta["fs-file-type"] = "reg"
            bos_client.copy_object(bucket, filename.encode("utf-8"),
                                   bucket, filename.encode("utf-8"), user_metadata=dest_user_meta)
    fs_file_attr.clear()

    for dir in dirs:
        filename = dir[0:len(dir) - 1]
        # bos_client.get_object_as_string(bucket, filename, [0, 1])
        dest_user_meta = dict()
        dest_user_meta["fs-mode"] = "%d" % (default_mode)
        dest_user_meta["fs-file-type"] = "dir"
        bos_client.put_object_from_string(bucket, filename.encode("utf-8"), "", user_metadata=dest_user_meta)
        bos_client.put_object_from_string(bucket, dir.encode("utf-8"), "", user_metadata=dest_user_meta)
        BosFsck(bucket, dir)


BosFsck(bucket)
