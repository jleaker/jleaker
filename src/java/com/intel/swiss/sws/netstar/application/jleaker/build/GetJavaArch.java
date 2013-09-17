package com.intel.swiss.sws.netstar.application.jleaker.build;

public class GetJavaArch
{
	public static void main(String[] args)
	{
		String arch = System.getProperty("os.arch");
		System.out.println(arch);
	}	
}
