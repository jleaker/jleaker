package com.intel.swiss.sws.netstar.application.jleaker.main;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.net.ServerSocket;
import java.net.Socket;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

public class JLeakerServer implements Runnable
{
	private static final int ACCEPT_TIMEOUT = (int)TimeUnit.SECONDS.toMillis(10);
	private ServerSocket m_serverSock = null;
	private AtomicBoolean m_wasStarted = new AtomicBoolean(false);
	
	public void waitForStart()
	{
		synchronized (m_wasStarted)
		{
			while (!m_wasStarted.get())
			{
				try
				{
					m_wasStarted.wait();
				}
				catch (InterruptedException e)
				{
				}
			}
		}
	}
	
	public int getPort()
	{
		return m_serverSock.getLocalPort();
	}
	
	@Override
	public void run()
	{
		Socket sock = null;
		try
		{
			m_serverSock = new ServerSocket(0);
			synchronized (m_wasStarted)
			{
				m_wasStarted.set(true);
				m_wasStarted.notifyAll();
			}
			m_serverSock.setSoTimeout(ACCEPT_TIMEOUT);
			sock = m_serverSock.accept();
			m_serverSock.close();
			BufferedReader br = new BufferedReader(new InputStreamReader(sock.getInputStream()));
			String line;
			while (null != (line = br.readLine()))
			{
				System.out.println(line);
			}
			br.close();
			m_serverSock = null;
		}
		catch (IOException e)
		{
			e.printStackTrace();
		}
		finally
		{
			if (null != m_serverSock)
			{
				try
				{
					m_serverSock.close();
				}
				catch (IOException e)
				{
				}
			}
			if (null != sock)
			{
				try
				{
					sock.close();
				}
				catch (IOException e)
				{
				}
			}
		}
	}
}
