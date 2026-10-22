## Column

`xlat.column.Column` is a class containing a list of `Signal` objects of equal width. The name derives from the use of
Column as a building block of a Table.

A Column is created from a list of values; the Column constructor will attempt to convert each value into a Signal if
it is not one already. It will then ensure that all the Signals are of equal width, otherwise it will raise a
`RuntimeError`.

Examples: 

    Column([True, True, False])  # Booleans are converted into Signals of width 1
    Column([True, Signal('-'), False])  # Types can be mixed
    Column([(4, 0), (4, 1), (4, 2), (4, 3)])  # Tuples will be passed on to Signal's constructor, so this will
                                              # create 4 Signals each of width 4, with values 0, 1, 2, 3
    Column(['1100', '1-01', '001-'])  # Strings work too
    
    Column([True, '1', '--'])  # RuntimeError: Widths don't match!
    Column([None, 'foobar'])  # RuntimeError: Not Signals, nor convertible to Signals!
    
### Length and Width

The _length_ of a Column is the number of Signals. The _width_ is the width of each Signal:

    c = Column(['11', '01', '-1'])
    len(c) == 3  # Use standard Python len() for length
    c.width == 2
    
### Slicing

A Column can be sliced, in two ways. A plain slice will slice the Column "vertically":

    c = Column(['011', '010', '1-1'])
    print(c[0]) ->
        1
        0
        1
    print(c[1]) ->
        1
        1
        -
    print(c[1:2]) ->
        01
        01
        1-
        
Notice that, just as for Signals, the upper bound is inclusive. In fact this kind of Column slice is nothing more than
the application of a Signal slice for each Signal in the Column.

If you want to slice "horizontally", you can use the `rows` property to do so:

    c = Column(['011', '010', '1-1'])
    print(c.rows[0]) -> 011
    print(c.rows[2]) -> 1-1
    print(c.rows[0:2]) -> [Signal(width=3, value=3, mask=7), Signal(width=3, value=2, mask=7)]
    
Notice two differences here: the sliced list of rows is not itself a Column, it is merely a Signal or list of Signals.
Also, the slice behaves like an normal Python slice with respect to the upper bound. I'm not sure if this is the
"right" behavior, so this may be an area to be revisited. It's hard to tell what would be the least surprising for the
user.

    


