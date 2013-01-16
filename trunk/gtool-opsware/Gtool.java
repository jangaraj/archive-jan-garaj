package gtool;

import com.opsware.client.*;
import com.opsware.common.LegacyException;
import com.opsware.common.NotFoundException;
import com.opsware.compliance.sco.SCODvcJobResult;
import com.opsware.fido.AuthenticationException;
import com.opsware.fido.AuthorizationException;
import com.opsware.job.*;
import com.opsware.osprov.ServerJobElementResult;
import com.opsware.script.ScriptJobTargetResult;
import com.opsware.search.Filter;
import com.opsware.search.SearchException;
import com.opsware.server.ServerRef;
import com.opsware.server.ServerService;
import com.opsware.server.ServerVO;
import com.opsware.swmgmt.DeviceJobResult;
import com.opsware.swmgmt.PolicyAssociation;
import com.opsware.swmgmt.PolicyAttachableReference;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;
import java.io.IOException;
import java.lang.reflect.InvocationTargetException;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.charset.Charset;
import java.rmi.RemoteException;
import java.text.DateFormat;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.logging.Level;
import java.util.logging.Logger;
import org.apache.commons.cli.*;

/**
 *
 * @author Jan Garaj - jan.garaj@gmail.com
 */
public class Gtool {
    
    public static final String CONFIGMESHES = "config-meshes.txt";
    public static final String COMMENTCHAR = ";";
    private static final Runtime runtime = Runtime.getRuntime ();
    
    /**
     * @param args the command line arguments
     */
    public static void main(String[] args) throws ParseException, Throwable {
        
        // argument handling/checking
        long gtime1 = System.currentTimeMillis();
        Options options = new Options();
        String action = null;
        String input = null;        
        options.addOption("m", true, "find in one mesh, param name of mesh");
        options.addOption("help", false, "print this message");
        options.addOption("fs", true, "part of finding name of server");
        options.addOption("fsd", false, "full server detail");
        options.addOption("fj", true, "findJob, param job id");
        options.addOption("fjd", false, "full job detail");
        options.addOption("js", true, "jobStat, param date");
        options.addOption("js2", false, "jobStat metric2 - 1server=1(sub)job");
        options.addOption("ss", false, "serverStat, no param");
        
        CommandLineParser parser = new PosixParser();
        CommandLine cmd = null;
        try {
            cmd = parser.parse(options, args);
        } catch (MissingArgumentException ex) {
            System.out.println("Can't find params for option:\n"+ex.getOption().toString());
            System.exit(1); 
        }        
              
        if(cmd.hasOption("fs")) {
            action = "findServer";
            input = cmd.getOptionValue("fs");
        }
        if(cmd.hasOption("fj")) {
            action = "findJob";
            input = cmd.getOptionValue("fj");
        } 
        if(cmd.hasOption("js")) {
            action = "jobStat";
            input = cmd.getOptionValue("js");
        }        
        if(cmd.hasOption("ss")) {
            action = "serverStat";
            input = "";
        }        
        if(cmd.hasOption("help") || args.length == 0 || action.isEmpty()) {
            Gtool.displayHelp();
            return;
        }
        
        // load/parse config file of meshes
        String sConfigMeshes = null;
        String pathFile = null;
        String findServer = args[0];
        ArrayList<MeshConnection> workMeshConnections;        
        try {
            pathFile = System.getProperty("user.dir") + "\\" + CONFIGMESHES;
            sConfigMeshes = Gtool.readFile(pathFile);
        } catch (IOException ex) {
            //Logger.getLogger(Opsware.class.getName()).log(Level.SEVERE, null, ex);
            System.out.println("Error on reading file " + pathFile);
        }        
        workMeshConnections = Gtool.getWorkMeshConnections(sConfigMeshes, cmd.getOptionValue("m"));
            
        // run in serial connects  (or parallel in threads)        
        Object result = null;        
        String connectResult = null;        
        long time1;
        java.lang.reflect.Method method = null;
        Gtool workObject = new Gtool();
        //System.out.print("action:"+action+" input:"+input);    
        try {
            method = workObject.getClass().getMethod(action, input.getClass(), cmd.getClass(), input.getClass());
        } catch (SecurityException e) {
            System.out.println("Debug 2: "+ e.toString());
        } catch  (NoSuchMethodException e) {
            System.out.println("Debug 2: "+ e.toString());            
        }
        for (int cycle = 0; cycle < workMeshConnections.size(); cycle++) {
            System.out.print("Mesh " + workMeshConnections.get(cycle).Mesh+": ");
            time1 = System.currentTimeMillis();
            connectResult = Gtool.connectMesh(workMeshConnections.get(cycle));
            //System.out.print(" " + OpswareClient.getServerAPIVersionLabel());
/*            
            try {                
                String testConnection = OpswareClient.getServerAPIVersion();
                
            } catch (AuthenticationException e) {
                result = "Debug 1:\n" + e.toString() + "\n" + e.getCause() + "\n\n";
               continue;
            }
*/            
            // run somenthing on mesh
            try {
                result = method.invoke(workObject, input, cmd, workMeshConnections.get(cycle).Mesh);
            } catch (IllegalArgumentException e) {
                result = "Debug 1:\n" + e.toString() + "\n" + e.getCause() + "\n\n";
            } catch (IllegalAccessException e ) {
                result = "Debug 1:\n" + e.toString() + "\n" + e.getCause() + "\n\n";
            } catch (InvocationTargetException e) {
                result = "Debug 1:\n" + e.toString() + "\n" + e.getCause() + "\n\n";
            }
            
            Gtool.disconnectMesh();
            System.out.println("\t\t["+(System.currentTimeMillis()-time1)+"ms, "+(Gtool.memoryUsed()/1024)+"kB]");
            System.out.print(connectResult+result.toString());
        }        
        System.out.print("\nTotal time: "+(System.currentTimeMillis()-gtime1)+"ms, Total used memory: "+(Gtool.memoryUsed()/1024)+"kB\n");
    }
    
    private static String readFile(String path) throws IOException {
        FileInputStream stream = new FileInputStream(new File(path));
        try {
            FileChannel fc = stream.getChannel();
            MappedByteBuffer bb = fc.map(FileChannel.MapMode.READ_ONLY, 0, fc.size());
            return Charset.defaultCharset().decode(bb).toString();
        } finally {
            stream.close();
        }
    }
     
    private static ArrayList getWorkMeshConnections(String configMeshes, String selectedMesh) {
        ArrayList<MeshConnection> workMeshConnections = new ArrayList<MeshConnection>();
        ArrayList<MeshConnection> allMeshConnections = new ArrayList<MeshConnection>();
        String[] temp;
        for (String line : configMeshes.split(System.getProperty("line.separator"))) {
            // skip empty and commented lines
            if (line.length() < 1 || line.substring(0, 1).equals(COMMENTCHAR)) {
                continue;
            }
            temp = line.split(COMMENTCHAR);
            allMeshConnections.add(new MeshConnection(temp[0], temp[1], temp[2], temp[3]));
            if (selectedMesh != null && selectedMesh.equals(temp[3])) {
                workMeshConnections.add(new MeshConnection(temp[0], temp[1], temp[2], temp[3]));
                return workMeshConnections;
            }
        }
        if (selectedMesh != null) {
            System.out.println("Can't find selected mesh in config file: " + selectedMesh);
            System.exit(1);
        }
        return allMeshConnections;
    }

    private static void displayHelp() {
        System.out.println(
        "HPSA Support Global Tool v0.1 - jan.garaj@gmail.com\n\n"+
        "Options:\n"+
        " -help\tprint this help message\n"+
        " -m\tselection mesh, input is mesh name from config file\n"+
        " -fs\tfind server, input is string in name/hostname\n"+
        " -fsd\tadd more details in find server (SW policies)\n"+
        " -fj\tfind job, input is JobId\n"+
        " -fjd\tadd more details in find job (problems in overall server status)\n"+
        " -js\tjobs statistic/dump, input is DATE1,DATE2 - date format YYYY-MM-DD\n"
        //"\t-ss\t\t\tserver stat function\n"
        );
    }
    
    public static String findServer(String input, CommandLine cmd, String mesh) throws Throwable {
        String output = "";
        ServerVO[] serverVOs = null;
        try {
            ServerService serverSvc = (ServerService)OpswareClient.getService(ServerService.class);            
            Filter filter = new Filter();
            filter.setExpression("(ServerVO.hostName *=* "+input+") | ("+"ServerVO.hostName *=* "+input+")");
            ServerRef[] serverRefs = null;
            try {
                serverRefs = serverSvc.findServerRefs(filter);
            } catch (RemoteException e) {
                output = e.toString() + "\n" + e.getCause() + "\n\n";
            } catch (AuthorizationException e) {
                output = e.toString() + "\n" + e.getCause() + "\n\n";
            } catch (SearchException e) {
                output = e.toString() + "\n" + e.getCause() + "\n\n";
            }
            serverVOs = serverSvc.getServerVOs(serverRefs);            
            
            for (ServerVO s : serverVOs) {                                
                output = output+s.getName()+"\n"; //("+s.getCustomer().getName()+"), ";
                output = output + s.toString() + "\n";
                output = output + "Discovered date: " + s.getDiscoveredDate() + "\n";
                output = output + "Lock: " + s.getLockInfo().isLocked() + " by " +s.getLockInfo().getUser()+  "\n";                
                output = output + "Customer: "+s.getCustomer().getName() + "\n";
                
                if (cmd.hasOption("fsd")) {
                    // SW policies
                    PolicyAssociation[] policies = serverSvc.getSoftwarePolicyAssociations((PolicyAttachableReference) s.getRef());
                    output = output + "SW Policies:\n";
                    for (PolicyAssociation apolicy : policies) {
                        output = output + apolicy.getPolicy().getName() + "\n";
                    }                    
                }
            }
            
        } catch (RemoteException e ) {
            output = e.toString() + "\n" + e.getCause() + "\n\n";
        } catch (NotFoundException e) {
            output = e.toString() + "\n" + e.getCause() + "\n\n";
        } catch (AuthorizationException e) {
            output = e.toString() + "\n" + e.getCause() + "\n\n";
        }
        if(output.length()>0) output = "Found "+serverVOs.length+" servers:\n"+output;
        else output = "Server not found";
        output = output.trim()+"\n";        
        return output;

    }
    
     public static String serverStat(String input, CommandLine cmd, String mesh) throws Throwable {
        String output = "";
        ServerVO[] serverVOs = null;
        try {
            ServerService serverSvc = (ServerService)OpswareClient.getService(ServerService.class);
            Filter filter = new Filter();
            filter.setExpression("(ServerVO.hostName *=* "+input+") | ("+"ServerVO.hostName *=* "+input+")");
            ServerRef[] serverRefs = null;
            try {
                serverRefs = serverSvc.findServerRefs(filter);
            } catch (RemoteException e) {
                output = e.toString() + "\n" + e.getCause() + "\n\n";
            } catch (AuthorizationException e) {
                output = e.toString() + "\n" + e.getCause() + "\n\n";
            } catch (SearchException e) {
                output = e.toString() + "\n" + e.getCause() + "\n\n";
            }
            serverVOs = serverSvc.getServerVOs(serverRefs);            
            ServerStat globalStat = new ServerStat();
            System.out.println(serverVOs.length+" :size\n");
            int tempp = 0;                    
            for (ServerVO s : serverVOs) {
                System.out.println(tempp + ":\n");
                tempp++;
                // output = output+s.getName()+", "; //("+s.getCustomer().getName()+"), ";
                System.out.println(s.toString());
                System.out.println("Stage:" + s.getStage());
                System.out.println("HPSA name: " + s.getName());
                System.out.println("HPSA Agent Version: " + s.getAgentVersion());
                System.out.println("OS Version: " + s.getOsVersion());
                System.out.println("Customer: " + s.getCustomer().getName());
                System.out.println("Last scan date: " + s.getLastScanDate());
            }
            output = output + globalStat.displayTable();
                         
        } catch (RemoteException e) {
            output = e.toString() + "\n" + e.getCause() + "\n\n";
        } catch (NotFoundException e ) {
            output = e.toString() + "\n" + e.getCause() + "\n\n";
        } catch (AuthorizationException e) {
            output = e.toString() + "\n" + e.getCause() + "\n\n";
        }
        return output; 
       
    }  
     
    public static String jobStat(String input, CommandLine cmd, String mesh) throws RemoteException, AuthorizationException, NotFoundException, SearchException, Throwable, InvocationTargetException {
        String output;
        JobService jobSvc = (JobService) OpswareClient.getService(JobService.class);
        String filterInput;
        Filter filter = new Filter();
        ServerVO[] serverVOs;
        DateFormat formatter = new SimpleDateFormat("yyyy-MM-dd");
        Date date;
        String dateString;

        if (input.equalsIgnoreCase("TODAY")) {
            filterInput = "(job_visible EQUAL_TO 1)&(job_group_id is_null 0)&(JobInfoVO.startDate IS_TODAY 0)";
            dateString = "Job statistic for today (depends also on mesh timezone):";
        } else {
            if (input.equalsIgnoreCase("LASTEEK")) {
                filterInput = "(job_visible EQUAL_TO 1)&(job_group_id is_null 0)&(JobInfoVO.startDate WITHIN_LAST_DAYS 7)";
                dateString = "Job statistic for last week (depends also on mesh timezone):";
            } else {
                String metric;
                if (cmd.hasOption("js2")) {                      
                    metric = "(job_visible EQUAL_TO 0)&(job_group_id is_null 1)&";
                } else {
                    metric = "(job_visible EQUAL_TO 1)&(job_group_id is_null 0)&";
                }
                if (input.indexOf(',') == 0) {
                    date = (Date) formatter.parse(input);
                    Calendar c = Calendar.getInstance();
                    c.setTime(formatter.parse(input));
                    c.add(Calendar.DATE, 1);
                    filterInput = metric+"(JobInfoVO.startDate BETWEEN " + date.getTime() + " AND " + (c.getTimeInMillis()) + ")";
                    dateString = "Job statistic for " + input + ":";
                } else {
                    String[] inputArray = input.split(",");
                    dateString = "Job statistic from " + inputArray[0] + " 00:00:00 to " + inputArray[1] + " 00:00:00 :";
                    //Calendar c = Calendar.getInstance();
                    //c.setTime(formatter.parse(input));
                    //c.add(Calendar.DATE, 1);
                    date = (Date) formatter.parse(inputArray[0]);
                    Date date2 = (Date) formatter.parse(inputArray[1]);
                    //(job_visible EQUAL_TO 1)&(job_group_id is_null 0)&
                    filterInput = metric+"(JobInfoVO.startDate BETWEEN " + date.getTime() + " AND " + date2.getTime() + ")";
                }
            }
        }

        output = dateString;
        HashMap jobIdStatusToString = new HashMap();
        //                  put(key, value)
        jobIdStatusToString.put(0, "Aborted ");
        jobIdStatusToString.put(1, "Active  ");
        jobIdStatusToString.put(2, "Cancelled");
        jobIdStatusToString.put(3, "Deleted ");
        jobIdStatusToString.put(4, "Failure ");
        jobIdStatusToString.put(5, "Pending ");
        jobIdStatusToString.put(6, "Success ");
        jobIdStatusToString.put(7, "Uknown  ");
        jobIdStatusToString.put(8, "Warning ");
        jobIdStatusToString.put(9, "Tampered");
        jobIdStatusToString.put(10, "Stale   ");
        jobIdStatusToString.put(11, "Blocked ");
        jobIdStatusToString.put(12, "Recurring");
        jobIdStatusToString.put(13, "Expired ");
        jobIdStatusToString.put(14, "Zombie  ");
        jobIdStatusToString.put(15, "Terminating");
        jobIdStatusToString.put(16, "Terminated");

        filter.setExpression(filterInput);
        JobRef[] jobRefs = jobSvc.findJobRefs(filter);
        JobInfoVO[] jobVOs = jobSvc.getJobInfoVOs(jobRefs);
        if (jobVOs.length == 0) {
            return "Nothing to display/calculate.\n\n";
        }
        JobStat globalStat = new JobStat();
        JobResult r2;
        DateFormat formatter2 = new SimpleDateFormat("yyyy-MM-dd HH:mm:ss");

        FileWriter writer = new FileWriter("job_statistic_" + input.replace(',', '_') + "_" + mesh + ".csv");
        writer.append("");
        writer.append(';');
        writer.append("Description");
        writer.append(';');
        writer.append("Status");
        writer.append(';');
        writer.append("Ticket ID");
        writer.append(';');
        writer.append("Start Time");
        writer.append(';');
        writer.append("End Time");
        writer.append(';');
        writer.append("User Name");
        writer.append(';');
        writer.append("# Servers");
        writer.append(';');
        writer.append("# Groups");
        writer.append(';');
        writer.append("Job ID");
        writer.append(';');
        writer.append("Type");
        //writer.append(';'); writer.append("xxx");        
        writer.append('\n');

        for (JobInfoVO s2 : jobVOs) {
            globalStat.count(s2.getStatus(), s2.getType());
            writer.append("");
            writer.append(';');
            writer.append(s2.getDescription());
            writer.append(';');
            writer.append(jobIdStatusToString.get(s2.getStatus()).toString());
            writer.append(';');
            writer.append("Ticket ID");
            writer.append(';');
            writer.append(formatter2.format(s2.getStartDate()));
            writer.append(';');
            try {
                writer.append(formatter2.format(s2.getEndDate()));
            } catch (NullPointerException e) {
                //System.out.println("Error on writing EndDate for jobID: " + s2.getRef().getIdAsLong().toString() + "\n\n"); // + e.toString() + "\n" + e.getCause() + "\n\n");
            }
            writer.append(';');
            writer.append(s2.getUserName());
            writer.append(';');
            writer.append(String.format("%d", s2.getServerInfo().length));
            writer.append(';');
            writer.append(String.format("%d", s2.getDeviceGroups().length));
            writer.append(';');
            writer.append(s2.getRef().getIdAsLong().toString());
            writer.append(';');
            writer.append(globalStat.humanType(s2.getType()));
            //writer.append(';');writer.append();            
            writer.append('\n');
            writer.flush();
        }
        output = output + "\n" + globalStat.displayTable();
        writer.close();
        return output;
    }    
    
    public static String findJob(String input, CommandLine cmd, String mesh) throws RemoteException, AuthorizationException, NotFoundException, SearchException, Throwable {
        String output = "";
        JobService jobSvc = (JobService) OpswareClient.getService(JobService.class);
        String filterInput;
        Filter filter = new Filter();
        ServerVO[] serverVOs;
        filterInput = "job_id = " + input;
        filter.setExpression(filterInput);
        JobRef[] jobRefs = jobSvc.findJobRefs(filter);
        JobInfoVO[] jobVOs = jobSvc.getJobInfoVOs(jobRefs);

        int i = 1;
        JobResult r2;
        for (JobInfoVO s2 : jobVOs) {
            r2 = jobSvc.getResult((JobRef) s2.getRef());
            output = r2.getJobInfo().toString() + "\nUser: " + r2.getJobInfo().getUserName() + "\n";
            output = output + "Servers#: " + s2.getServerInfo().length + "\n";
            

            if (cmd.hasOption("fjd")) {                                
                HashMap serverNames = new HashMap();
                for (JobServerInfo jobServer : s2.getServerInfo()) {
                    serverNames.put(jobServer.getServer().getId(), jobServer.getServer().getName());
                }

                output = output + "Problems:\n";
                if ((r2 instanceof MultiElementJobResult)) {
                    String serverTempName = "";
                    JobElementResult[] localObject = ((MultiElementJobResult) r2).getElemResultInfo();

                    if (localObject != null) {
                        for (int ii = 0; ii < localObject.length; ii++) {
                            JobElementResult sk = (JobElementResult) localObject[ii];
                            //output = output + sk + "\n";
                                                       
                            // ServerJobElementResult
                            if (sk instanceof ServerJobElementResult) {                                
                                ServerRef serverName = (ServerRef) localObject[ii].getElement();
                                if ((Object) serverName.getId() != null) {
                                    if(serverNames.get(serverName.getId())!=null) serverTempName = ">>> " + serverNames.get(serverName.getId()) + ": ";
                                    else serverTempName = ">>> Uknown server: ";
                                } else {
                                    //System.out.println("null");
                                }
                                                                
                                ServerJobElementResult sk2 = (ServerJobElementResult) sk;
                                String error = null;
                                if ((sk2.getError() != null) && (sk2.getError().getLocalizedMessage() != null) && (sk2.getError().getLocalizedMessage().trim().length() > 0)) {
                                    error = sk2.getError().getMessage();
                                } else if ((sk2.getError() != null) && ((sk2.getError() instanceof LegacyException))) {
                                    error = new StringBuilder().append(sk2.getError().toString()).append(((LegacyException) sk2.getError()).getLegacyErrorCode()).toString();
                                } else if (sk2.getError() != null) {
                                    error = sk2.getError().toString();
                                }
                                // server name also in sk2.getElement().getName()
                                if(error!=null) output = output + serverTempName + "\nError: " + error + "\n\n"; 
                            } 
                            
                            // DeviceJobResult
                            if (sk instanceof DeviceJobResult) {
                                ServerRef serverName = (ServerRef) localObject[ii].getElement();
                                if ((Object) serverName.getId() != null) {
                                    if(serverNames.get(serverName.getId())!=null) serverTempName = ">>> " + serverNames.get(serverName.getId()) + ":\n";
                                    else serverTempName = ">>> Uknown server:\n";
                                } else {
                                    //System.out.println("null");
                                }   
                                
                                DeviceJobResult sk2 = (DeviceJobResult) sk;                                                           
                                System.out.println(sk2.getStatus());
                                if ("error".equals(sk2.getStatus()) || "warning".equals(sk2.getStatus())) { // && sk2.getErrorInfo().getTraceback()!=null) {
                                    String error = "";
                                    try {
                                        error = "Error: " + sk2.getErrorInfo().getKey();
                                    } catch (NullPointerException e) {
                                        //output = output + serverTempName;
                                        //break;
                                    }

                                    try {
                                        if (sk2.getErrorInfo().getTraceback() != null) {
                                            error = error + "\nTraceback: " + sk2.getErrorInfo().getTraceback();
                                        }
                                    } catch (NullPointerException e) {
                                        //
                                    }
                                    if (sk2.getWarningInfos() != null) {
                                        JobMessageInfo[] localWarns = sk2.getWarningInfos();
                                        if (localWarns.length > 0) {
                                            error = error + "Warning: ";
                                            for (i = 0; i < localWarns.length; i++) {
                                                error = error + localWarns[i].getKey() + " ";
                                            }
                                        }
                                    }                                    
                                    if (error != null || error.isEmpty()!=false) {
                                        output = output + serverTempName + error + "\n\n";
                                    }
                                }
                                
                            }
                            
                            // ScriptJobTargetResult
                            if (sk instanceof ScriptJobTargetResult) {
                                ServerRef serverName = (ServerRef) localObject[ii].getElement();
                                if ((Object) serverName.getId() != null) {
                                    if(serverNames.get(serverName.getId())!=null) serverTempName = ">>> " + serverNames.get(serverName.getId()) + ": ";
                                    else serverTempName = ">>> Uknown server: ";
                                } else {
                                    //System.out.println("null");
                                }
                                ScriptJobTargetResult sk2 = (ScriptJobTargetResult) sk;
                                String error = null;
                                if ((sk2.getError() != null) && (sk2.getError().getLocalizedMessage() != null) && (sk2.getError().getLocalizedMessage().trim().length() > 0)) {
                                    error = sk2.getError().getMessage();
                                } else if ((sk2.getError() != null) && ((sk2.getError() instanceof LegacyException))) {
                                    error = new StringBuilder().append(sk2.getError().toString()).append(((LegacyException) sk2.getError()).getLegacyErrorCode()).toString();
                                } else if (sk2.getError() != null) {
                                    error = sk2.getError().toString();
                                }
                                // server name also in sk2.getElement().getName()
                                if(error!=null) output = output + serverTempName + "\nError: " + error + "\n\n"; 
                            } 
                            
                            // SCODvcJobResult
                            if (sk instanceof SCODvcJobResult) {
                                SCODvcJobResult sk2 = (SCODvcJobResult) sk;
                                serverTempName = ">>> " + sk2.getElement().getName() + ":\n";
                                String error = null;
                                if (sk2.getError() != null) {
                                    error = "Error: " + sk2.getError().toString() + "\n";
                                }
                                if(sk2.getWarnings() != null) {
                                    String[] localWarns = sk2.getWarnings();
                                    if (localWarns.length > 0) {
                                        error = error + "Warning: ";
                                        for (i = 0; i < localWarns.length; i++) {
                                            error = error + "  " + localWarns[i] + " ";
                                        }
                                    }
                                }
                                if (error != null) {
                                    output = output + serverTempName + error + "\n\n";
                                }
                            }                            

                        }
                    }

                }

            }
            i++;
        }

        if (output.length() == 0) {
            output = "JobId not found";
        }
        output = output.trim() + "\n";
        return output;

    }
    
    public static long memoryUsed () {
        return runtime.totalMemory () - runtime.freeMemory ();
    }
   
    private static String connectMesh(MeshConnection get) {
        try {
            OpswareClient.disconnect();
        } catch (CloseFailed ex) {
            //return "CloseFailed";
        }        
        try {
            OpswareClient.connect("https", get.Server, (short)443, get.User , get.Password, true);            
        } catch (AuthenticationException ex) {
            return "Connecting AuthenticationException\n";
        } catch (RemoteException ex) {
            //Logger.getLogger(GtoolFindServer.class.getName()).log(Level.SEVERE, null, ex);
            return "Connecting RemoteException\n";
        }         
        return "";
    }

    private static void disconnectMesh() {
        try {
            OpswareClient.disconnect();
        } catch (CloseFailed ex) {
            System.out.println("disconnectMesh failed");
        }
    }
}
class MeshConnection {
    public String Server;
    public String User;
    public String Password;
    public String Mesh;
    
    public MeshConnection(String iServer, String iUser, String iPassword, String iMesh) {
        this.Server = iServer;
        this.User = iUser;
        this.Password = iPassword;
        this.Mesh = iMesh;        
    }
}

class JobStat{
    HashMap meshStats = new HashMap();        
    HashMap jobIdStatusToString = new HashMap();    
    HashMap jobTypeToString = new HashMap();        
    HashMap temp = new HashMap();
    ArrayList <HashMap> workMeshStats = new ArrayList<HashMap>();
    int total = 0;
    
    public JobStat() {
        jobIdStatusToString.put(0, "Aborted ");
        jobIdStatusToString.put(1, "Active  ");
        jobIdStatusToString.put(2, "Cancelled");
        jobIdStatusToString.put(3, "Deleted ");
        jobIdStatusToString.put(4, "Failure ");
        jobIdStatusToString.put(5, "Pending ");
        jobIdStatusToString.put(6, "Success ");
        jobIdStatusToString.put(7, "Uknown  ");
        jobIdStatusToString.put(8, "Warning ");
        jobIdStatusToString.put(9, "Tampered");
        jobIdStatusToString.put(10, "Stale   ");
        jobIdStatusToString.put(11, "Blocked ");
        jobIdStatusToString.put(12, "Recurring");
        jobIdStatusToString.put(13, "Expired ");
        jobIdStatusToString.put(14, "Zombie  ");
        jobIdStatusToString.put(15, "Terminating");
        jobIdStatusToString.put(16, "Terminated");

        jobTypeToString.put("server.swpolicy.remediate", "Remediate Policies");
        jobTypeToString.put("server.script.run", "Run Server Script");
        jobTypeToString.put("server.patch.install", "Install Patch");
        jobTypeToString.put("ogfs.script.run", "Run OGFS Script");
        jobTypeToString.put("server.software.install", "Install Software");
        jobTypeToString.put("server.software.uninstall", "Uninstall Software");
        jobTypeToString.put("server.reboot", "Reboot Server");
        jobTypeToString.put("server.appconfig.scan", "Scan Configuration Compliance");
        jobTypeToString.put("server.run.customextension", "Run Custom Extension");
        jobTypeToString.put("server.os.install", "Run OS Sequence");
        jobTypeToString.put("server.audit.create", "Audit Servers");
        //jobTypeToString.put("", "");

        temp.put(0, 0);
        temp.put(1, 0);
        temp.put(2, 0);
        temp.put(3, 0);
        temp.put(4, 0);
        temp.put(5, 0);
        temp.put(6, 0);
        temp.put(7, 0);
        temp.put(8, 0);
        temp.put(9, 0);
        temp.put(10, 0);
        temp.put(11, 0);
        temp.put(12, 0);
        temp.put(13, 0);
        temp.put(14, 0);
        temp.put(15, 0);
        temp.put(16, 0);

        workMeshStats.add(temp);

    }
    
    public String humanType(String type) {
        if (jobTypeToString.get(type) != null) {
            return jobTypeToString.get(type).toString();
        } else {
            return type;
        }
    }
    
    public void count(int status, String type) {
        // global count
        Integer x = new Integer(workMeshStats.get(0).get(status).toString());
        int i = x.intValue();
        i++;
        workMeshStats.get(0).put(status, i);
        total++;
    }
    
    public String displayTable() {
        String output = "";
        Iterator it = workMeshStats.get(0).entrySet().iterator();
        while (it.hasNext()) {
            Map.Entry pairs = (Map.Entry) it.next();
            output = output + jobIdStatusToString.get(pairs.getKey()) + "\t " + pairs.getValue() + "\t\n";
            //output = output + Math.round(( (int) pairs.getValue()*100/total)) +"%\n";
            //System.out.println(pairs.getKey() + ":\t " + pairs.getValue());
            it.remove(); // avoids a ConcurrentModificationException
        }
        output = output + "Total   " + "\t " + total + "\t" + "100%\n\n";
        return output;
    }
}

class ServerStat {

    HashMap meshStats = new HashMap();
    int total = 0;

    public void count(String status) {
        // global count
        Integer x = new Integer(meshStats.get(status.toString()).toString());
        int i = x.intValue();
        i++;
        meshStats.put(status, i);
        total++;
    }

    public String displayTable() {
        String output = "";
        Iterator it = meshStats.entrySet().iterator();
        while (it.hasNext()) {
            Map.Entry pairs = (Map.Entry) it.next();
            output = output + pairs.getKey() + "\t " + pairs.getValue() + "\t\n";
            //output = output + Math.round(( (int)pairs.getValue()*100/total)) +"%\n";
            //System.out.println(pairs.getKey() + ":\t " + pairs.getValue());
            it.remove(); // avoids a ConcurrentModificationException
        }
        output = output + "Total   " + "\t " + total + "\t" + "100%\n\n";
        return output;
    }
}