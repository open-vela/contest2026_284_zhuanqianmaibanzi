// SPDX-License-Identifier: Apache-2.0
using System;
using System.Net;
using VelaPoka;
class ReceiverHarness
{
    static void Main(string[] args)
    {
        using (ReceiverServer server = new ReceiverServer(args[0], IPAddress.Loopback,
               IPAddress.Loopback, 0, delegate(string text) { Console.Error.WriteLine(text); },
               args.Length > 1 ? Int32.Parse(args[1]) : 30000))
        {
            server.Start();
            Console.WriteLine(server.Port);
            Console.Out.Flush();
            Console.ReadLine();
        }
    }
}
