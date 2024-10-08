#!/usr/bin/env python3
"""
Add/delete user

Verify that it is possible to add/delete a user. The user password is hashed
with yescrypt.
"""
import infamy
import copy
import random
import string
import re
import infamy.ssh as ssh
import infamy.util as util

username = "newuser01"
password = "newuser01password"
hashed_password = "$y$j9T$SALT$gx4K1wugXsm3JDLwYZtnbr37FGGvljotXwIBGOxaGf2"
with infamy.Test() as test:
    with test.step("Set up topology and attach to target DUT"):
        env = infamy.Env()
        target = env.attach("target", "mgmt")
        tgtssh = env.attach("target", "mgmt", "ssh")

    with test.step("Add new user 'newuser01' with password 'newuser01password'"):
        target.put_config_dict("ietf-system", {
            "system": {
                "authentication": {
                    "user": [
                        {
                            "name": username,
                            "password": hashed_password,
                            "shell": "infix-system:bash"
                        }
                    ]
                }
            }
        })

    with test.step("Verify user 'newuser01' exist in operational"):
        running = target.get_config_dict("/ietf-system:system")
        users = running["system"]["authentication"]["user"]
        user_found = False
        for user in users:
            if user["name"] == username:
                user_found = True
                assert user["password"] == hashed_password
                break
        assert user_found, f"User 'newuser01' not found"

    with test.step("Verify user 'newuser01' can login with SSH"):
        newssh = env.attach("target", "mgmt", "ssh", None, username, password)
        util.until(lambda: newssh.run("ls").returncode == 0, attempts=200)

    with test.step("Delete user 'newuser01'"):
        target.delete_xpath(f"/ietf-system:system/authentication/user[name='{username}']")

    with test.step("Verify erasure of user 'newuser01'"):
        running = target.get_config_dict("/ietf-system:system")
        users = running["system"]["authentication"]["user"]
        for user in users:
            assert user["name"] != username, f"User {username} not deleted"

    with test.step("Verify that 'newuser01' is removed from /etc/passwd"):
        util.until(lambda: tgtssh.runsh(f"sudo grep '{username}:' /etc/passwd") != 0)
    test.succeed()
