# -*- sh -*-
# REMEMBER TO UPDATE infix-interfaces ALSO IN confd.inc
MODULES=(
    "infix-interfaces@2024-11-27.yang -e vlan-filtering -e containers"
    "infix-containers@2024-11-15.yang"
)
