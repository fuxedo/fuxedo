VIEW v_all
# type  cname  fbname  count flag size null
short   vs     VSHORT  1     -    -    7
long    vl     VLONG   1     -    -    -1
char    vc     VCHAR   1     -    -    'Z'
float   vf     VFLOAT  1     -    -    -.5
double  vd     VDOUBLE 1     -    -    2.5
string  vstr   VSTRING 1     -    20   "hi"
carray  vca    VCARRAY 1     -    10   -
END
VIEW v_cl
# type  cname  fbname  count flag size null
short   nums   VNUMS   3     C    -    9
string  names  VNAMES  2     CL   10   'ab'
carray  items  VITEMS  2     CL   8    -
END
VIEW v_dir
# direction flags: F only, S only, N
long    ftob   VLONG   1     F    -    -
long    btos   VEAGE   1     S    -    -
long    none   VCOUNT  1     N    -    -
END
VIEW v_expr
string  ename  VENAME  1     -    20   -
long    eage   VEAGE   1     -    -    -
float   emark  VEMARK  1     -    -    -
END
VIEW v_prop
string  street VESTREET 1    P    10   'abc'
string  city   VECITY  1     -    12   'xyz'
END
VIEW v_conv
long    salary VSALARY 1     -    -    0
string  desc   VDESC   1     -    30   -
long    total  VTOTAL  1     -    -    NONE
END
