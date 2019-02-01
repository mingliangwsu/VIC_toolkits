#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Thu Nov 26, 2018 LIU 
Convert crop fractions into VIC veg parametger files for running VIC-CropSyst V2 (Forecast project)
@author: liuming
"""

import pandas as pd
import sys 
import os

if len(sys.argv) != 4:
    print("Usage:" + sys.argv[0] + "<original_soil_parameter_file> <new_sub_set> <out_new_soil_parameter_file>\n")
    sys.exit(0)

orig_soil_parameter_file = sys.argv[1]
subset_soil_parameter_file = sys.argv[2]
new_orig_soil_parameter_file = sys.argv[3]

subset_soil_dic = {}
#read soil parameter to create location dictionary
with open(subset_soil_parameter_file) as f:
    for line in f:
        a = line.split()
        if len(a) > 0:
            cellid = a[1]
            subset_soil_dic.update({cellid : line})
            #print(location + ':' + cellid)
print("reading subset soil done!")

outfile_soil = open(new_orig_soil_parameter_file,"w")

with open(orig_soil_parameter_file) as f:
    for line in f:
        a = line.split()
        if len(a) > 0:
            cellid = a[1]
            if cellid in subset_soil_dic:
                outline = subset_soil_dic[cellid]
            else:
                outline = line
            outfile_soil.write(outline)
outfile_soil.close()
print("Done!")
