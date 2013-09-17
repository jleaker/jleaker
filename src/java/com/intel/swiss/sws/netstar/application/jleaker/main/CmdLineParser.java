package com.intel.swiss.sws.netstar.application.jleaker.main;

import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.Map;
import java.util.Map.Entry;


public class CmdLineParser
{
	private final LinkedList<String> m_args;
	private final Map<String, String> m_switches = new HashMap<String, String>();
	private final Map<String, Object> m_mpValues = new HashMap<String, Object>();
	
	CmdLineParser(String[] args, String[] switches)
	{
		m_args = new LinkedList<String>(Arrays.asList(args));
		for (String sw : switches)
		{
			int idx = sw.indexOf('=');
			m_switches.put(sw.substring(0, idx), sw.substring(idx + 1));				
		}
	}
	
	void parse() throws CmdLineParserException
	{
		while (!m_args.isEmpty())
		{
			String arg = m_args.removeFirst();
			if (!arg.startsWith("--") || arg.length() == 2 || arg.startsWith("---"))
			{
				throw new CmdLineParserException("Unknown switch: " + arg);
			}
			arg = arg.substring(2);
			String switchProp = m_switches.get(arg);
			if (null == switchProp)
			{
				throw new CmdLineParserException("Unsupported switch: " + arg);				
			}
			String value = null;
			if (!switchProp.contains("b"))
			{
				if (m_args.isEmpty())
				{
					throw new CmdLineParserException("Argument " + arg + " requires a value");
				}
				value = m_args.removeFirst();
			}
			try
			{
				if (switchProp.contains("i"))
				{
					Integer num = Integer.parseInt(value);
					m_mpValues.put(arg, num);
				}
				else if (switchProp.contains("b"))
				{
					m_mpValues.put(arg, Boolean.TRUE);
				}
				else
				{
					m_mpValues.put(arg, value);
				}
			}
			catch (NumberFormatException e)
			{
				throw new CmdLineParserException("Value for argument " + arg + " is not a number");
			}
		}
		for (Entry<String, String> sw : m_switches.entrySet())
		{
			if (sw.getValue().contains("m") && !m_mpValues.containsKey(sw.getKey()))
			{
				throw new CmdLineParserException("Mandatory command line switch `--" + sw.getKey() + "' was not provided");
			}
		}
	}
	
	Object getValue(String arg)
	{
		return m_mpValues.get(getStrippedArg(arg));
	}

	private String getStrippedArg(String arg)
	{
		return arg.substring(0, arg.indexOf('='));
	}

	public boolean exists(String arg)
	{
		return m_mpValues.containsKey(getStrippedArg(arg));
	}
}
