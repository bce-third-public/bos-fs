#!/usr/bin/env python
#coding=utf-8

#导入Python标准日志模块
import logging

#从Python SDK导入BOS配置管理模块以及安全认证模块
from baidubce.bce_client_configuration import BceClientConfiguration
from baidubce.auth.bce_credentials import BceCredentials
#导入BOS相关模块
from baidubce import exception
from baidubce.services import bos
from baidubce.services.bos import canned_acl
from baidubce.services.bos.bos_client import BosClient

def GetBosClient(host, ak, sk):
    # 设置日志文件的句柄和日志级别
    logger = logging.getLogger('baidubce.services.bos.bosclient')
    fh = logging.FileHandler("sample.log")
    fh.setLevel(logging.DEBUG)

    # 设置日志文件输出的顺序、结构和内容
    formatter = logging.Formatter('%(asctime)s - %(name)s - %(levelname)s - %(message)s')
    fh.setFormatter(formatter)
    logger.setLevel(logging.DEBUG)
    logger.addHandler(fh)

    # 创建BceClientConfiguration
    config = BceClientConfiguration(credentials=BceCredentials(ak, sk), endpoint=host)
    return BosClient(config)

