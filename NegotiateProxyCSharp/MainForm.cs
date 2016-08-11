using System;
using System.Windows.Forms;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Text;
using System.IO;
using System.IO.Compression;
using System.Runtime.Serialization.Formatters.Binary;
using System.Collections.Generic;


namespace NegotiateProxyCSharp
{
    public partial class MainForm : Form
    {
        public MainForm()
        {
            InitializeComponent();
        }

        public static ManualResetEvent acceptDone = new ManualResetEvent(false);
        
        private void enElements(bool en)
        {
            // Активируем\деактивируем элементы управления главной формы
            startButton.Enabled = en;
            lPort.Enabled = en;
            rPort.Enabled = en;
            rAddr.Enabled = en;
            useTarget.Enabled = en;
            targetBox.Enabled = en;
            label1.Enabled = en;
            label2.Enabled = en;
            label3.Enabled = en;
            label4.Enabled = en;
            toolStripMenuItem1.Enabled = en;
            toolStripMenuItem2.Enabled = en;
            stopButton.Enabled = !en;
        }

        private void startButton_Click(object sender, EventArgs e)
        {
            string targetName = "";
            listener Listener = new listener((int)lPort.Value, (int)rPort.Value, rAddr.Text, useTarget.Checked, targetBox.Text, ref targetName);
            // Деактивируем элемены формы
            if (Listener.OK())
            {
                if (!useTarget.Checked)
                    targetBox.Text = targetName;
                enElements(false);
            } 
        }

        private void stopButton_Click(object sender, EventArgs e)
        {
            while(connection.connections.Count > 0)
            {
                connection.connections[0].Stop();
            }
            connection.remoteEP = null;

            // Разблокировываем элементы управления формы
            enElements(true);
        }

        private void MainForm_Shown(object sender, EventArgs e)
        {
            try
            {
                FileStream file = new FileStream(Application.StartupPath + "\\settings.set", FileMode.Open);

                GZipStream decompressionStream = new GZipStream(file, CompressionMode.Decompress);
                BinaryFormatter serializer = new BinaryFormatter();
                settings Sett = new settings();
                Sett = (settings)serializer.Deserialize(decompressionStream);

                lPort.Value = Sett.lPort;
                rPort.Value = Sett.rPort;
                rAddr.Text = Sett.rAddr;
                targetBox.Text = Sett.targetName;
                useTarget.Checked = Sett.useTarget;
                
                decompressionStream.Close();
                file.Close();
            }
            catch (Exception)
            {

            }
        }

        private void MainForm_FormClosed(object sender, FormClosedEventArgs e)
        {
            try
            {
                FileStream file = new FileStream(Application.StartupPath + "\\settings.set", FileMode.Create);
                GZipStream compressionStream = new GZipStream(file, CompressionMode.Compress);
                BinaryFormatter serializer = new BinaryFormatter();
                settings Sett = new settings();

                Sett.lPort = (int)lPort.Value;
                Sett.rPort = (int)rPort.Value;
                Sett.rAddr = rAddr.Text;
                Sett.targetName = targetBox.Text;
                Sett.useTarget = useTarget.Checked;

                serializer.Serialize(compressionStream, Sett);
                compressionStream.Close();
                file.Close();
            }
            catch (IOException)
            {

            }
        }

        private void useTarget_CheckedChanged(object sender, EventArgs e)
        {
            targetBox.ReadOnly = !useTarget.Checked;
        }

        private void toolStripMenuItem4_Click(object sender, EventArgs e)
        {
            Close();
        }

        private void MainForm_Deactivate(object sender, EventArgs e)
        {
            if (WindowState == FormWindowState.Minimized)
            {
                Visible = false;
            }
        }

        private void notifyIcon1_DoubleClick(object sender, EventArgs e)
        {
            if (WindowState == FormWindowState.Minimized)
            {
                Visible = true;
                WindowState = FormWindowState.Normal;
            }
            else
            {
                WindowState = FormWindowState.Minimized;
                Visible = false;
            }
        }
    }

    public abstract class connection
    {
        public static EndPoint remoteEP = null;

        public static List<connection> connections;

        protected static string targetName;

        protected static string hostName;

        protected Thread thread;

        public Socket sock = null;

        public connection other = null;

        protected const int BufferSize = 4096;

        protected byte[] buff = new byte[BufferSize];

        protected bool stop = false;

        protected ManualResetEvent done = new ManualResetEvent(false);

        protected static int getRespCode(string resp)
        {
            int pos = resp.IndexOf(' ');
            if (pos > 0)
            {
                int code;
                try
                {
                    if (int.TryParse(resp.Substring(++pos, 3), out code))
                        return code;
                    else
                        return -2;
                }
                catch (Exception) { return -3; }

            }
            else return -1;
        }

        protected static string getAutchMethod(string resp)
        {
            int pos = resp.IndexOf("proxy-authenticate: ", StringComparison.OrdinalIgnoreCase);

            if (pos > 0)
            {
                string tmp = resp.Substring(pos + "proxy-authenticate: ".Length);
                return tmp.Substring(0, tmp.IndexOf('\r')).ToLower();
            }
            else return "";
        }

        public void transmit()
        {
            // Создаем новый поток
            thread = new Thread(trans);
            // Делаем поток фоновым, чтобы он закрывался вместе с главным окном
            thread.IsBackground = true;
            // Запускаем поток
            thread.Start();
        }

        private void trans()
        {
            // Пока не остановили поток
            while (!stop)
            {
                try
                {
                    // Задаем несигнальное состояние события
                    done.Reset();

                    // Начинаем приём данных
                    sock.BeginReceive(buff, 0, BufferSize, 0,
                    new AsyncCallback(ReceiveCallback), this);

                    // Ждём пока произойдет чтение (блокируем поток)
                    done.WaitOne();
                }
                catch (Exception)
                {
                    //MessageBox.Show(exc.Message, "Ошибка начала приема данных");
                    Stop();
                }

                // Даём выполниться другим ожидающим потокам
                Thread.Sleep(0);
            }
            sock.Close();
        }

        protected void ReceiveCallback(IAsyncResult ar)
        {
            if (!stop)
            {
                try
                {
                    // Заканчиваем приём данных
                    int bytesRead = sock.EndReceive(ar);

                    // Если данные есть
                    if (bytesRead > 0)
                    {
                        if (GetType() == typeof(clientConnection))
                        {
                            // Сохраняем текущее сообщение
                            buff.CopyTo(((clientConnection)this).previousMsg, 0);
                            // Пересылаем их
                            other.sock.BeginSend(buff, 0, bytesRead, SocketFlags.None,
                                    new AsyncCallback(SendCallback), other.sock);
                        }
                        else
                        {
                            // Получаем код ответа от сервера
                            int code = getRespCode(Encoding.ASCII.GetString(buff, 0, 20));
                            // Если сервер запретил доступ 
                            if (code == 407)
                            {
                                string autchMethod = getAutchMethod(Encoding.ASCII.GetString(buff, 0, bytesRead));
                                if (autchMethod == "negotiate")
                                {
                                    // проходим аутентификацию
                                    byte[] autchBuf = Encoding.ASCII.GetBytes(((serverConnection)this).setAuthBuf());

                                    sock.Close();

                                    // Переподключаемся к серверу
                                    ((serverConnection)this).connect(remoteEP);

                                    // Заново шлем на сервер данные с добавленным токеном аутентификации
                                    sock.BeginSend(autchBuf, 0, autchBuf.Length, SocketFlags.None,
                                    new AsyncCallback(SendCallback), sock);
                                }
                                else
                                {
                                    // Устанавливаем сигнальное состояние события (разблокирываем основной поток)
                                    done.Set();
                                    Stop();
                                    MessageBox.Show(string.Format("Метод аутентификации '{0}' не поддерживается", autchMethod), "Ошибка");
                                }
                            }
                            else
                                // Иначе пересылаем данные клиенту
                                other.sock.BeginSend(buff, 0, bytesRead, SocketFlags.None,
                                    new AsyncCallback(SendCallback), other.sock);
                        }
                    }
                    else
                    {
                        // Устанавливаем сигнальное состояние события (разблокирываем основной поток)
                        done.Set();
                        Stop();
                    }
                }
                catch (Exception)
                {
                    //MessageBox.Show(exc.Message, "Ошибка чтения");
                    // Устанавливаем сигнальное состояние события (разблокирываем основной поток)
                    done.Set();
                    Stop();
                }
            }
        }

        protected void SendCallback(IAsyncResult ar)
        {
            if (!stop)
            {
                try
                {
                    // Устанавливаем сигнальное состояние события (разблокирываем основной поток)
                    done.Set();
                    // Получаем сокет, который шлёт данные
                    Socket sock = (Socket)ar.AsyncState;

                    // Завершаем отправку данных
                    int bytesSent = sock.EndSend(ar);
                    if (bytesSent == 0)
                    {
                        Stop();
                    }
                }
                catch (Exception)
                {
                    //MessageBox.Show(exc.Message, "Ошибка отправления данных");
                    Stop();
                }
            }  
        }

        public void Stop()
        {
            stop = true;
            connections.Remove(this);
            done.Set();
        }
    }

    public class listener : connection
    {
        public listener(int lPort, int rPort, string rAddr, bool useTarget, string target, ref string retTarget)
        {
            try
            {
                connections = new List<connection>();
                connections.Add(this);
                // Получаем информацию об удалённом адресе
                IPHostEntry rHostInfo = Dns.GetHostEntry(rAddr);
                // Извлекаем из этой информации IP
                IPAddress rAddress = rHostInfo.AddressList[0];
                
                hostName = rHostInfo.HostName;

                if (useTarget)
                {
                    targetName = target;
                    retTarget = "";
                }
                else
                {
                    targetName = buildTargetName(hostName);
                    retTarget = targetName;
                }


                try
                {
                    // Создаём прослушивающий сокет
                    sock = new Socket(
                        AddressFamily.InterNetwork,
                        SocketType.Stream,
                        ProtocolType.Tcp);

                    // Настраиваем порт на котором будем слушать
                    IPEndPoint localEP = new IPEndPoint(IPAddress.Any, lPort);

                    // Привязываем порт к сокету
                    sock.Bind(localEP);

                    // Начинаем слушать (100 подключений)
                    sock.Listen(100);

                    // Настраиваем точку подключения
                    remoteEP = new IPEndPoint(rAddress, rPort);

                    // Создаем новый поток
                    thread = new Thread(accept);
                    // Делаем поток фоновым, чтобы он закрывался вместе с главным окном
                    thread.IsBackground = true;
                    // Запускаем поток
                    thread.Start();
                }
                catch (Exception exc)
                {
                    MessageBox.Show(exc.Message, "Ошибка");
                    Stop();
                }
            }
            catch (Exception exc)
            {
                // Сообщение при ошибке
                MessageBox.Show(exc.Message, "Ошибка");
            }
        }

        private static string buildTargetName(string hostName)
        {

            // Get domain name
            string domain = "";

            for (int i = hostName.Length - 1, point = 0; i >= 0; i--)
            {
                if (hostName[i] == '.')
                    point++;

                if (point == 2)
                {
                    domain = hostName.Substring(i + 1);
                    break;
                }
            }

            domain = domain.ToUpper();

            return string.Format("HTTP/{0}@{1}", hostName, domain);
        }

        public bool OK()
        {
            return remoteEP != null;
        }

        private void accept()
        {
            // Пока не остановили поток
            while (!stop)
            {
                try
                {
                    // Задаем несигнальное состояние события
                    done.Reset();

                    // Начинаем асинхронный приём подключений
                    sock.BeginAccept(
                        new AsyncCallback(AcceptCallback),
                        sock);

                    // Ждём пока произойдет подключение (блокируем поток)
                    done.WaitOne();
                }
                catch (Exception exc)
                {
                    // Сообщение при ошибке
                    MessageBox.Show(exc.Message, "Ошибка начала принятия входящего подключения");
                    Stop();
                }

                // Даём выполниться другим ожидающим потокам
                Thread.Sleep(0);
            }
            
            sock.Close();
        }

        private void AcceptCallback(IAsyncResult ar)
        {
            try
            {
                if (!stop)
                {
                    // Создаем объекты подключения
                    clientConnection lState = new clientConnection();
                    serverConnection rState = new serverConnection();

                    lState.other = rState;
                    rState.other = lState;

                    // Принимаем подключение и записываем его в локальный сокет
                    lState.sock = sock.EndAccept(ar);

                    //MessageBox.Show("Accept");
                    // Подключаемся к прокси
                    rState.connect(remoteEP);

                    // Добавляем подключения в лист
                    connections.Add(lState);
                    connections.Add(rState);

                    // Устанавливаем сигнальное состояние события (разблокирываем основной поток)
                    done.Set();

                    // Принимаем данные от клиента и пересылаем их серверу
                    lState.transmit();

                    // Принимаем данные от сервера и пересылаем их клиенту
                    rState.transmit();
                }
            }
            catch (Exception exc)
            {
                // Сообщение при ошибке
                MessageBox.Show(exc.Message, "Ошибка принятия входящего подключения");
                // Устанавливаем сигнальное состояние события (разблокирываем основной поток)
                done.Set();
            }

        }
    }

    public class clientConnection : connection
    {
        public byte[] previousMsg = new byte[BufferSize];
    }

    public partial class serverConnection : connection
    {
        public void connect(EndPoint EP)
        {
            // Создаем сокет для подключения к прокси
            sock = new Socket(AddressFamily.InterNetwork, SocketType.Stream, ProtocolType.Tcp);
            // Начинаем подключение
            sock.Connect(EP);
        }
    }

    /* НАСТРОЙКИ ПРОГРАММЫ */
    [Serializable]
    public class settings
    {
        public int lPort;
        public int rPort;
        public string rAddr;
        public string targetName;
        public bool useTarget;
    }

}
