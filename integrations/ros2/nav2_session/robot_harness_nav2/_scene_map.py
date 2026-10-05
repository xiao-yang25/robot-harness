"""Startup occupancy checks for the explicit, bounded occupied-A test scene."""
import math

from ._predicates import GOALS


def occupied_a_sample(message):
    info = message.info
    origin = info.origin
    values = (info.resolution, origin.position.x, origin.position.y, origin.position.z,
              origin.orientation.x, origin.orientation.y, origin.orientation.z,
              origin.orientation.w)
    if (message.header.frame_id != 'map' or not all(math.isfinite(v) for v in values)
            or info.resolution <= 0 or not 0 < info.width <= 10000
            or not 0 < info.height <= 10000 or len(message.data) != info.width * info.height
            or (origin.orientation.x, origin.orientation.y, origin.orientation.z,
                origin.orientation.w) != (0., 0., 0., 1.)):
        raise ValueError('unsupported actual scene map geometry')
    def cell(x, y):
        col = math.floor((x-origin.position.x)/info.resolution)
        row = math.floor((y-origin.position.y)/info.resolution)
        if not (0 <= col < info.width and 0 <= row < info.height):
            raise ValueError('registered scene site outside actual map')
        return message.data[row*info.width+col]
    sample = dict(frame='map', width=info.width, height=info.height,
        resolution=info.resolution, origin=[origin.position.x, origin.position.y, origin.position.z],
        a_cell=cell(*GOALS['A']), b_cell=cell(*GOALS['B']), start_cell=cell(-2., -.5))
    if (sample['a_cell'], sample['b_cell'], sample['start_cell']) != (100, 0, 0):
        raise ValueError('occupied-A/free-B/free-start scene map mismatch')
    return sample
