# SPDX-License-Identifier: GPL-2.0-or-later
#
# This file is part of osm2pgsql (https://osm2pgsql.org/).
#
# Copyright (C) 2023 by the osm2pgsql developer community.
# For a full list of authors see the git log.


class ReplicationServerMock:

    def __init__(self):
        self.expected_base_url = None
        self.state_infos = []


    def __call__(self, base_url):
        assert self.expected_base_url is not None and base_url == self.expected_base_url,\
               f"Wrong replication service called. Expected '{self.expected_base_url}', got '{base_url}'"
        return self


    def get_state_info(self, seq=None, retries=2):
        assert self.state_infos, 'Replication mock not properly set up'
        if seq is None:
            return self.state_infos[-1]

        for info in self.state_infos:
            if info.sequence == seq:
                return info

        assert False, f"No sequence information for sequence ID {seq}."

    def timestamp_to_sequence(self, timestamp, balanced_search=False):
        assert self.state_infos, 'Replication mock not properly set up'

        if timestamp < self.state_infos[0].timestamp:
            return self.state_infos[0].sequence

        prev = self.state_infos[0]
        for info in self.state_infos:
            if timestamp >= prev.timestamp and timestamp < info.timestamp:
                return prev.sequence
            prev = info

        return prev.sequence

    def apply_diffs(self, handler, start_id, max_size=1024, idx="", simplify=True):
        if start_id > self.state_infos[-1].sequence:
            return None

        numdiffs = int((max_size + 1023)/1024)
        return min(self.state_infos[-1].sequence, start_id + numdiffs - 1)

