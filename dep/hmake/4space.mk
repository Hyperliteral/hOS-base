# This Makefile uses 4-space indentation in places other than recipes

# 1. Indented variable assignment (sometimes done for readability)
    MY_VARIABLE = accidental_recipe

# 2. Indented conditional block
.if 1 == 1
    # This comment is indented with 4 spaces
    TARGET_NAME = value
.endif

all:
    @echo "Normal target working as expected"