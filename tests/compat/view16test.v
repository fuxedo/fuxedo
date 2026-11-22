VIEW v16_all
# type  cname  fbname  count flag size null
short   vs     VS16     1     -    -    7
long    vl     VL16    1     -    -    -1
char    vc     VC16     1     -    -    'Z'
float   vf     VF16  1     -    -    -.5
double  vd     VD16 1     -    -    2.5
string  str    VSTR16    1     -    20   "hi"
carray  ca     VCA16 1     -    10   -
END
VIEW v16_cl
# counted members exercise the ACM/ALM layout
short   nums   VNUMS16     3     C    -    9
string  names  VNAMES16    2     CL   10   'ab'
END
