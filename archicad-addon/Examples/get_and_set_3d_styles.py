import aclib

# The 3D styles of the project (Document > 3D Styles) and the one the 3D window uses.
# SetCurrent3DStyle was dropped (it crashed Archicad): read the styles, and change them with Change3DStyles.
styles = aclib.RunTapirCommand ('Get3DStyles')
print ('Current style: {}'.format (styles['currentStyleName']))
aclib.RunTapirCommand ('Get3DStyleDetails', {'names': [styles['currentStyleName']]})
