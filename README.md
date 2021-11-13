# Original Readme

[Readme](README.rst)

# CMake For Fastbuild Context
* [FASTBuild generator](https://gitlab.kitware.com/cmake/cmake/-/issues/15294)
* [WIP: Add Fastbuild generator](https://gitlab.kitware.com/cmake/cmake/-/merge_requests/4200)

# My Changes
+ 1. Extra validation for empty Target.LinkerNodes.
+ 2. Replace the ":" in fullpath when converting a path to fastbuild target name.
+ 3. Calculate the right i18n number for the linker lib.
+ 4. Support the response file for a long commander line.
+ 5. Put the link result to the normal directory as the other CMake Generator.

After I read the context, I found that it last for about 4 years and is not released yet up to now.
I don't have time to make such a MR, so I only publish it here.
I will be appreciate if someone can make it merged back to the main branch.

My changes works for my large project(by disable the pch), but I am not sure it works for all the others.
I can support but I am not responsable for these changes.

# License

All my commits(signed off as LuDong<<noodle1983@126.com>>) are in public domain.

