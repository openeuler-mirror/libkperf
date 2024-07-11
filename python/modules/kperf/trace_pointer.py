"""
Copyright (c) Huawei Technologies Co., Ltd. 2024. All rights reserved.
libkperf licensed under the Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
    http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR
PURPOSE.
See the Mulan PSL v2 for more details.
Author: Mr.Li
Create: 2024-07-16
Description: kperf trace pointer field obtain
"""
class PointerCommData:
    common_type = "common_type"
    common_flags = "common_flags"
    common_preempt_count = "common_preempt_count"
    common_pid = "common_pid"


class NetNetifRx(PointerCommData):
    event_name = "net:netif_rx"

    name = "name"
    skbaddr = "skbaddr"
    len = "len"


class SkebSkbCopyDatagramIovec(PointerCommData):
    event_name = "skb:skb_copy_datagram_iovec"

    skbaddr = "skbaddr"
    len = "len"


class NetApiGroReceiveEntry(PointerCommData):
    event_name = "net:napi_gro_receive_entry"

    name = "name"
    napi_id = "napi_id"
    queue_mapping = "queue_mapping"
    skbaddr = "skbaddr"
    vlan_tagged = "vlan_tagged"
    vlan_proto = "vlan_proto"
    vlan_tci = "vlan_tci"
    protocol = "protocol"
    ip_summed = "ip_summed"
    hash = "hash"
    l4_hash = "l4_hash"
    len = "len"
    data_len = "data_len"
    truesize = "truesize"
    mac_header_valid = "mac_header_valid"
    mac_header = "mac_header"
    nr_frags = "nr_frags"
    gso_size = "gso_size"
    gso_type = "gso_type"


class PointerEvt:
    NetApiGroReceiveEntry = NetApiGroReceiveEntry
    NetNetifRx = NetNetifRx
    SkebSkbCopyDatagramIovec = SkebSkbCopyDatagramIovec


__all__ = ['PointerEvt']
