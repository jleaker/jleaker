package com.intel.swiss.sws.netstar.application.jleaker.main;

import java.io.File;
import java.io.FileNotFoundException;
import java.util.StringTokenizer;

import com.sun.tools.attach.VirtualMachine;

public class JLeaker
{
	private static final int DEFAULT_SIZE_THRESHOLD = 300;
	private static final int DEFAULT_REFERENCE_CHAIN_LENGTH = 60;

	private static final String ARG_LIB_PATH = "lib-path=m";
	private static final String ARG_CONF_PATH = "conf-path=m";
	private static final String ARG_SIZE_THRESHOLD = "size-threshold=i";
	private static final String ARG_PID = "pid=im";
	private static final String ARG_REFERENCE_CHAIN_LENGTH = "reference-chain-length=i";
	private static final String ARG_MAX_FAN_IN = "max_fan_in=i";
	private static final String ARG_DEBUG = "debug=b";
	private static final String ARG_SHOW_UNREACHABLES = "show-unreachables=b";
	private static final String ARG_SELF_CHECK = "self-check=b";
	private static final String ARG_NO_GC = "no-gc=b";
	private static final String ARG_CONF_FILE = "conf-file=s";
	private static final String ARG_CONSIDER_LOCAL_REF = "consider-local-references=b";
	private static final String[] ALL_ARGS = {
		ARG_LIB_PATH,
		ARG_CONF_PATH,
		ARG_SIZE_THRESHOLD, 
		ARG_PID, 
		ARG_REFERENCE_CHAIN_LENGTH,
		ARG_MAX_FAN_IN,
		ARG_DEBUG,
		ARG_SELF_CHECK,
		ARG_CONF_FILE,
		ARG_NO_GC,
		ARG_SHOW_UNREACHABLES,
		ARG_CONSIDER_LOCAL_REF
	};
	private int m_sizeThreshold;
	private int m_referenceChainLength;
	private int m_pid;
	private String m_libPath;
	private String m_confPath;
	private String m_more_options = "";
	
	public static void main(String[] args) throws Exception
	{
		JLeaker jleaker = new JLeaker();
		try
		{
			jleaker.parse(args);
			jleaker.execute();
		}
		catch (CmdLineParserException e)
		{
			System.out.println(e.getMessage());
			System.out.println();
			jleaker.printUsage();
			System.exit(1);
		}
	}

	private void printUsage()
	{
		System.out.println("Usage:");
		System.out.println("jleaker --pid <JAVA PID> [options]");
		System.out.println("Options are:");
		System.out.println("\t--size-threshold <num> \t\tAlert only on data structures with size bigger than <num> (default is " + DEFAULT_SIZE_THRESHOLD + ")");
		System.out.println("\t--reference-chain-length <num> \tMax number of references to iterate when searching for the reference chain to root (default is " + DEFAULT_REFERENCE_CHAIN_LENGTH + ")");
		System.out.println("\t--debug \t\t\tEnable verbose logging in JLeaker");
		System.out.println("\t--show-unreachables \t\tDon't try to find reference chain to root for leaking objects, display all direct reference to it instead");
		System.out.println("\t--self-check \t\t\tUse JLeaker to check itself for memory leaks");
		System.out.println("\t--no-gc \t\t\tDon't run garbage collection prior to the memory leak scanning (Default is to run GC)");
		System.out.println("\t--conf-file <FILES> \t\tA list of JLeaker configuration files, separated by a '" + File.pathSeparatorChar + "' character");
		System.out.println("\t--consider-local-references \tConsider local variable references and JNI local references as a heap root references (Default: No)");
		System.out.println();
	}

	private void parse(String[] args) throws Exception
	{
		CmdLineParser parser = new CmdLineParser(args, ALL_ARGS);
		parser.parse();
		m_libPath = (String)parser.getValue(ARG_LIB_PATH);
		m_confPath = (String)parser.getValue(ARG_CONF_PATH);
		findSizeThreshold(parser);
		m_pid = (Integer)parser.getValue(ARG_PID);
		findReferenceChainLength(parser);
		Integer maxFanIn = (Integer)parser.getValue(ARG_MAX_FAN_IN);
		boolean debug = parser.exists(ARG_DEBUG);
		boolean self_check = parser.exists(ARG_SELF_CHECK);
		boolean show_unreachables = parser.exists(ARG_SHOW_UNREACHABLES);
		boolean no_gc = parser.exists(ARG_NO_GC);
		boolean consider_local_ref = parser.exists(ARG_CONSIDER_LOCAL_REF);
		String confFile = (String)parser.getValue(ARG_CONF_FILE);
		final String defaultConf = m_confPath + File.separator + "jleaker.conf";
		if (debug)
		{
			m_more_options += "debug,";
		}
		if (self_check)
		{
			m_more_options += "self_check,";
		}
		if (null != maxFanIn)
		{
			m_more_options += "max_fan_in=" + maxFanIn + ",";
		}
		if (show_unreachables)
		{
			m_more_options += "show_unreachables,";
		}
		if (no_gc)
		{
			m_more_options += "no_gc,";			
		}
		if (consider_local_ref)
		{
			m_more_options += "consider_local_references,";
		}
		if (null != confFile)
		{
			StringTokenizer st = new StringTokenizer(confFile, File.pathSeparator);
			StringBuilder nomalizedConfFiles = new StringBuilder();
			if (st.hasMoreElements())
			{
				while (st.hasMoreElements())
				{
					File f = new File(st.nextToken());
					if (!f.isAbsolute())
					{
						f = new File(m_confPath + File.separator + f.getPath());
					}
					if (!f.canRead())
					{
						throw new Exception("Configuration file " + f.getPath() + " is not accessible");
					}
					nomalizedConfFiles.append(f.getPath()).append(File.pathSeparator);
				}
				nomalizedConfFiles.append(defaultConf);
				confFile = nomalizedConfFiles.toString();
			}
		}
		else
		{
			confFile = defaultConf;
		}
		m_more_options += "conf_file=" + confFile + ",";			
	}

	private void findSizeThreshold(CmdLineParser parser)
	{
		final Integer sizeThreshold = (Integer)parser.getValue(ARG_SIZE_THRESHOLD);
		m_sizeThreshold = (null == sizeThreshold)?DEFAULT_SIZE_THRESHOLD:sizeThreshold;
	}

	private void findReferenceChainLength(CmdLineParser parser)
	{
		Integer refLength = (Integer)parser.getValue(ARG_REFERENCE_CHAIN_LENGTH);
		m_referenceChainLength = (null == refLength)?DEFAULT_REFERENCE_CHAIN_LENGTH:refLength;
	}
	
	private void execute() throws Exception
	{
		VirtualMachine vm = VirtualMachine.attach(Integer.toString(m_pid));
		
		String arch = (String)vm.getSystemProperties().get("os.arch");
		
		final File libFile = new File(m_libPath.replace("<ARCH>", arch));
		if (!libFile.exists())
		{
			throw new FileNotFoundException("Cannot find agent library at " + libFile.getPath());
		}
		m_libPath =libFile.getAbsolutePath();
		JLeakerServer server = startServer();
		server.waitForStart();
		vm.loadAgentPath(m_libPath, m_more_options + "size_threshold=" + m_sizeThreshold + ",reference_chain_length=" + m_referenceChainLength + ",tcp_port=" + server.getPort());
		vm.detach();
		
	}

	protected JLeakerServer startServer()
	{
		JLeakerServer server = new JLeakerServer();
		Thread t = new Thread(server);
		t.setName("JLeakerServerThread");
		t.start();
		return server;
	}
	
}
