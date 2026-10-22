## Signal

`xlat.signal.Signal` is a class for representing a tri-state value of a particular bit-width.
 
Each bit of a Signal may be zero, one, or "don't-care". A don't-care bit might be represented by any of the 
characters "?", "-", "x", or "z" depending on the context. The Signal class makes no distinction between these
"flavors" of don't-care (unlike SystemVerilog).

A Signal may be created in a number of ways:  

    from xlat.signal import Signal
    
    Signal(False)  # a one-bit wide 0
    Signal(True)  # a one-bit wide 1
    Signal('101')  # from a binary string...
    Signal('1-1')  # ...or a trinary string
    Signal('1x1')  # same as the above
    Signal(3, 5)  # width 3, value 5 (same as '101')
    Signal(3, 5, 0b101)  # width 3, value 5, mask 5 (in binary)...same as '1-1'
    Signal(width=3, value=5, mask=0b101)  # same as above, but with labeled arguments for clarity

Note that the mask of a Signal marks the bits which are _definite_. Those bits with a corresponding mask bit of
zero are don't-cares.

#### Identity

Signals are _object identical_, in the sense that there is only one object for all Signals with the same width, value,
and mask.

    Signal(False) is Signal('0')  # True!
    Signal('1-1') is Signal(width=3, value=5, mask=5)  # True!
    Signal(width=8, 87) is Signal(width=9, 87)  # False! Different widths!
    
This allows the creation of Signals to be common and cheap (memory-wise, at least).

#### Sub-Signals

The `[]` operator can be used to extract new Signals from existing Signals. It works like a normal Python index or
slice, with the exception that the upper bound is _inclusive_. This variation is more natural for people used to bit
manipulation in RTL.

    Signal('10')[0] == Signal('0')
    Signal('10')[1] == Signal('1')
    
Notice that the least significant (0th) bit is on the right of bit string.

    Signal('11-0')[1:2] == Signal('1-')
    
Stepping works too.

    Signal('1-1-1-')[1:5:2] == Signal('111')
    
If your index is out of range, Signal with raise an `IndexError` exception.

#### Conversions

Under the right circumstances, a Signal can be converted into a boolean or integer value.

###### Conversion to Boolean

A one bit Signal with a zero or one value can be converted to a boolean. For example:

    if Signal('1'):
        print("it's true!")
        
will print "it's true!". Any other kind of Signal (width greather than one, or `Signal('-')`) will raise
`xlat.signal.NotABoolean`.

##### Conversion to Integer

Any Signal without don't-cares can be converted into an integer.

    int(Signal('1001')) == 9
    
If the Signal has a don't-care, an `xlat.signal.IndefiniteValue` exception will be raised.

#### Printing/String Conversion

Signals are rendered as trinary strings with "-" as the don't-care bit by default.

    str(Signal('01xx1')) == '01--1'
    
But with a format specifier (`:sv`), it's possible to produce a SystemVerilog compatible value with "?" as the don't-care:

    f"{Signal('01xx1'):sv}" == "5'b01??1"
