Global Tool v0.1 for
You can use my source code as example for your Opsware (HP Server Automation) API tools.
It's global tool, because this tool can workw with many meshes (depends on configuration in file config-meshes.txt).
It's lightweight version of oficial Opsware client.
You need java 1.6+. Copy into lib directory opswclient.jar (from your occ) and commons-cli-1.2.jar

Know issues:
- it's not possible to use '-' in input for -fs (for example rp-au-citrix02)
- rarely gtool is hanged, use CTRL+C for aborting
- if you have InvocationTargetException on some mesh, you can try use option -m on problem mesh

Try some examples:
# display help for gtool
java -jar gtool.jar

# in which mesh is server servername, mentioned in my ticket
java -jar gtool.jar -fs servername

# has server servername.local in mesh1 attached Default Inventory policy (DDMI)?
java -jar gtool.jar -fs servername -m mesh1 -fsd

# find all servers with name *example.com in mesh3
java -jar gtool.jar -m mesh3 -fs example.com

# find basic information about job id 1095380001 in mesh1
java -jar gtool.jar -m mesh1 -fj 1095380001

# dump not hidden jobs into csv files in working directory (for statistic) from 2012-04-10 to 2012-04-15 for all meshes
java -jar gtool.jar -js 2012-04-10,2012-04-12

# tip: you can use standard redirect output to file by >
java -jar gtool.jar -m mesh3 -fs example.com > all_servers_example_com_in_mesh3.txt