#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@author: liuming
seperate soil parameter file into US & CA part according to met data availability
"""

#import pandas as pd
import sys 
import os

if len(sys.argv) == 4:
    print("Usage:" + sys.argv[0] + "<original_soil_parameter_file>\n")
    sys.exit(0)

orig_soil_parameter_file = sys.argv[1]
out_US = 'US_' + orig_soil_parameter_file
out_CA = 'CA_' + orig_soil_parameter_file

US_met = '/data/adam/data/metdata/historical/UI_historical/VIC_Binary_CONUS_to_2016/data_'
CA_met = '/data/adam/data/metdata/historical/Ben_170522_VIC_Binary/data_'

outfile_US = open(out_US,"w")
outfile_CA = open(out_CA,"w")
us_lines = 0
ca_lines = 0
with open(orig_soil_parameter_file) as f:
    for line in f:
        a = line.split()
        if len(a) > 0:
            cellid = a[1]
            lat = a[2]
            lon = a[3]
            us_met = US_met + lat + '_' + lon
            ca_met = CA_met + lat + '_' + lon
            if os.path.exists(us_met):
                outfile_US.write(line)
                us_lines += 1
            elif os.path.exists(ca_met):
                outfile_CA.write(line)
                ca_lines += 1
outfile_US.close()
outfile_CA.close()
if us_lines == 0:
    os.remove(out_US)
if ca_lines == 0:
    os.remove(out_CA)
print("Done!")
