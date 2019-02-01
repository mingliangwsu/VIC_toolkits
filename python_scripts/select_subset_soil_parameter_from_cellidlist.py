#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
@author: liuming

Create a subset of soil parameter file from cell list
"""

import pandas as pd
import sys 
import os

if len(sys.argv) != 4:
    print("Usage:" + sys.argv[0] + "<original_soil_parameter_file> <cell_list> <out_new_soil_parameter_file>\n")
    sys.exit(0)

orig_soil_parameter_file = sys.argv[1]
subset = sys.argv[2]
new_orig_soil_parameter_file = sys.argv[3]

subset_list = list()
#read soil parameter to create location dictionary
with open(subset) as f:
    for line in f:
        cell = line.rstrip()
        if len(cell) > 0:
            subset_list.append(cell)
            #print(location + ':' + cellid)
print("reading subset soil done!")

outfile_soil = open(new_orig_soil_parameter_file,"w")

with open(orig_soil_parameter_file) as f:
    for line in f:
        a = line.split()
        if len(a) > 0:
            cellid = a[1]
            if cellid in subset_list:
                outfile_soil.write(line)
outfile_soil.close()
print("Done!")
