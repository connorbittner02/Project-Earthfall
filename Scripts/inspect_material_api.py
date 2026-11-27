import unreal
for name in dir(unreal.MaterialEditingLibrary):
    if any(key in name.lower() for key in ('input', 'expression')):
        unreal.log_warning('MEL API ' + name)
for name in ('connect_material_expressions', 'get_material_expression_input_names', 'get_inputs_for_material_expression'):
    unreal.log_warning('MEL DOC ' + name + ' ' + str(getattr(unreal.MaterialEditingLibrary, name).__doc__))
for path in ['/Game/Earth/Materials/M_EarthPhotoreal', '/Game/Earth/Materials/M_EarthClouds']:
    mat = unreal.load_asset(path)
    unreal.log_warning('MATERIAL ' + path)
    if hasattr(unreal.MaterialEditingLibrary, 'get_material_expressions'):
        for expr in unreal.MaterialEditingLibrary.get_material_expressions(mat):
            cls = expr.get_class().get_name()
            if cls in ('MaterialExpressionOneMinus', 'MaterialExpressionComponentMask', 'MaterialExpressionSaturate'):
                unreal.log_warning('EXPR ' + cls + ' ' + str(expr.get_name()))
                try:
                    unreal.log_warning(' INPUT NAMES ' + str(unreal.MaterialEditingLibrary.get_material_expression_input_names(mat, expr)))
                except Exception as exc:
                    unreal.log_warning(' INPUT NAME ERROR ' + str(exc))
                try:
                    unreal.log_warning(' INPUTS ' + str(unreal.MaterialEditingLibrary.get_inputs_for_material_expression(mat, expr)))
                except Exception as exc:
                    unreal.log_warning(' INPUT ERROR ' + str(exc))
