After installing a static object for adis in ../install/lib:

% python3 setup.py build
% python3 setup.py install --user

usage:

% python3
>>> import adis
>>> help(adis)
>>> adis.dis(0x6e034000)
'6e034000 1bf: EXT V0.16B, V0.16B, V3.16B, #8'
