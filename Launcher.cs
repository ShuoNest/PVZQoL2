// PVZQoL2. Native animated pickup and garden-only cabbage appearance.
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

static class Native {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("kernel32.dll")] public static extern ulong GetTickCount64();
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr VirtualAllocEx(IntPtr process,IntPtr addr,UIntPtr size,uint kind,uint protect);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool VirtualFreeEx(IntPtr process,IntPtr addr,UIntPtr size,uint kind);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool WriteProcessMemory(IntPtr process,IntPtr addr,byte[] data,UIntPtr size,out UIntPtr written);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
    [DllImport("kernel32.dll",CharSet=CharSet.Ansi)] static extern IntPtr GetProcAddress(IntPtr module,string name);
    [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr CreateRemoteThread(IntPtr process,IntPtr attributes,UIntPtr stack,IntPtr start,IntPtr arg,uint flags,IntPtr tid);
    [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle,uint timeout);
    [DllImport("kernel32.dll")] static extern bool GetExitCodeThread(IntPtr handle,out uint code);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    static Exception Error(string name) { return new Exception(name+"："+new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()).Message); }
    public static void LoadInto(Process game,string dll) {
        IntPtr process=OpenProcess(0x43A,false,game.Id),remote=IntPtr.Zero,thread=IntPtr.Zero;
        bool finished=false;
        if(process==IntPtr.Zero) throw Error("无法连接游戏");
        try {
            byte[] data=Encoding.Unicode.GetBytes(Path.GetFullPath(dll)+"\0");
            remote=VirtualAllocEx(process,IntPtr.Zero,(UIntPtr)data.Length,0x3000,4);
            if(remote==IntPtr.Zero) throw Error("无法分配加载空间");
            UIntPtr written;
            if(!WriteProcessMemory(process,remote,data,(UIntPtr)data.Length,out written)||written.ToUInt64()!=(ulong)data.Length) throw Error("无法写入模块路径");
            IntPtr load=GetProcAddress(GetModuleHandle("kernel32.dll"),"LoadLibraryW");
            if(load==IntPtr.Zero) throw Error("无法定位系统加载函数");
            thread=CreateRemoteThread(process,IntPtr.Zero,UIntPtr.Zero,load,remote,0,IntPtr.Zero);
            if(thread==IntPtr.Zero) throw Error("无法加载扩展");
            finished=WaitForSingleObject(thread,10000)==0;
            if(!finished) throw new Exception("加载超时；请正常退出游戏后重试。");
            uint result;GetExitCodeThread(thread,out result);
            if(result==0) throw new Exception("Windows 未能载入 PVZQoL2.dll。");
        } finally {
            if(thread!=IntPtr.Zero)CloseHandle(thread);
            if(remote!=IntPtr.Zero&&(thread==IntPtr.Zero||finished))VirtualFreeEx(process,remote,UIntPtr.Zero,0x8000);
            CloseHandle(process);
        }
    }
}

sealed class MainForm : Form {
    static readonly string GamePath=Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"..","Replanted.exe"));
    const string AssemblyHash="E7333EAF87996FA44B4A3526986272319B7D01F7ACDBCFF52451652F61CEB696";
    readonly string folder=AppDomain.CurrentDomain.BaseDirectory;
    readonly CheckBox sun=new CheckBox(),money=new CheckBox(),cabbage=new CheckBox(),autoRake=new CheckBox();
    readonly CheckBox autoFeed=new CheckBox(),autoFertilizer=new CheckBox(),autoBugSpray=new CheckBox(),autoTreeFood=new CheckBox();
    readonly Button launch=new Button();readonly Label status=new Label(),stats=new Label();
    readonly NumericUpDown delay=new NumericUpDown();
    readonly Timer timer=new Timer();
    MemoryMappedFile map;MemoryMappedViewAccessor view;Process game;
    bool busy,connected,closing;ulong loadTime,lastDiagnostic;string lastState="等待启动";
    public MainForm(bool auto) {
        AutoScaleMode=AutoScaleMode.None;
        Text="植物大战僵尸 · 游戏助手";ClientSize=new Size(700,480);
        Font=new Font("Microsoft YaHei UI",14,FontStyle.Regular,GraphicsUnit.Pixel);FormBorderStyle=FormBorderStyle.FixedDialog;
        MaximizeBox=false;StartPosition=FormStartPosition.CenterScreen;
        var title=new Label {Text="PVZQoL2  ·  花园与智慧树自动化",Font=new Font(Font.FontFamily,20,FontStyle.Bold,GraphicsUnit.Pixel),AutoSize=true,Location=new Point(22,18)};
        Controls.Add(title);
        sun.Text="自动拾取阳光";money.Text="自动拾取金币 / 钻石";cabbage.Text="花园卷心菜彩蛋外观";
        autoRake.Text="干草叉用完自动购买（扣金币）";
        autoFeed.Text="持续自动喂智慧树（每 3 秒尝试一次）";
        autoFertilizer.Text="花园肥料用完自动购买（5 份 / 包）";
        autoBugSpray.Text="杀虫剂用完自动购买（5 份 / 包）";
        autoTreeFood.Text="树肥用完自动购买（1 份 / 包）";
        CheckBox[] checks={sun,money,cabbage,autoRake,autoFeed,autoFertilizer,autoBugSpray,autoTreeFood};
        for(int i=0;i<checks.Length;i++){var c=checks[i];c.Checked=true;c.AutoSize=true;c.Location=new Point(i<4?25:345,65+(i%4)*32);c.CheckedChanged+=delegate{ApplySettings();};Controls.Add(c);}
        Controls.Add(new Label {Text="补购按商店实际价格扣金币；喂树仅在智慧树界面运行，关闭开关即停止。",AutoSize=true,Location=new Point(25,198),ForeColor=Color.DimGray});
        Controls.Add(new Label {Text="拾取等待帧数",AutoSize=true,Location=new Point(25,231)});
        delay.Minimum=5;delay.Maximum=120;delay.Value=25;delay.Location=new Point(160,227);delay.Size=new Size(70,28);delay.ValueChanged+=delegate{ApplySettings();};Controls.Add(delay);
        Controls.Add(new Label {Text="25 帧约 0.5 秒（原速）",AutoSize=true,Location=new Point(242,231),ForeColor=Color.DimGray});
        launch.Text="启动游戏并启用";launch.Size=new Size(650,38);launch.Location=new Point(25,268);launch.Click+=delegate{StartOrAttach();};Controls.Add(launch);
        status.Location=new Point(25,316);status.Size=new Size(650,25);status.Text="等待启动";Controls.Add(status);
        stats.Location=new Point(25,347);stats.Size=new Size(650,125);stats.Font=new Font(Font.FontFamily,12,FontStyle.Regular,GraphicsUnit.Pixel);stats.ForeColor=Color.DimGray;Controls.Add(stats);
        LoadSettings();timer.Interval=250;timer.Tick+=delegate{Tick();};timer.Start();
        using(var graphics=CreateGraphics()) {
            float scale=graphics.DpiX/96f;
            if(scale>1.01f) {
                Scale(new SizeF(scale,scale));
                Font=new Font("Microsoft YaHei UI",14*scale,FontStyle.Regular,GraphicsUnit.Pixel);
                title.Font=new Font(Font.FontFamily,20*scale,FontStyle.Bold,GraphicsUnit.Pixel);
                stats.Font=new Font(Font.FontFamily,12*scale,FontStyle.Regular,GraphicsUnit.Pixel);
            }
        }
        Shown+=delegate {if(auto)StartOrAttach();};
        FormClosing+=delegate {closing=true;if(view!=null){try{view.Write(8,0);}catch{}}timer.Stop();WriteDiagnostic("工具已关闭，功能已停用");ReleaseMap();};
    }
    void LoadSettings() {
        try { foreach(string line in File.ReadAllLines(Path.Combine(folder,"settings.ini"))) {
            string[] p=line.Split('=');if(p.Length!=2)continue;
            if(p[0]=="sun")sun.Checked=p[1]=="1";if(p[0]=="money")money.Checked=p[1]=="1";
            if(p[0]=="gardenCabbage")cabbage.Checked=p[1]=="1";
            if(p[0]=="autoRake")autoRake.Checked=p[1]=="1";
            if(p[0]=="autoFeedTree")autoFeed.Checked=p[1]=="1";
            if(p[0]=="autoFertilizer")autoFertilizer.Checked=p[1]=="1";
            if(p[0]=="autoBugSpray")autoBugSpray.Checked=p[1]=="1";
            if(p[0]=="autoTreeFood")autoTreeFood.Checked=p[1]=="1";
            int v;if(p[0]=="delay"&&int.TryParse(p[1],out v))delay.Value=Math.Min(120,Math.Max(5,v));
        }} catch {} ApplySettings();
    }
    int Flags { get {return(sun.Checked?1:0)|(money.Checked?2:0)|(cabbage.Checked?8:0)|(autoRake.Checked?16:0)|(autoFertilizer.Checked?32:0)|(autoBugSpray.Checked?64:0)|(autoTreeFood.Checked?128:0)|(autoFeed.Checked?256:0);} }
    void ApplySettings() {
        try {if(view!=null){view.Write(8,Flags);view.Write(56,(uint)delay.Value);}
            File.WriteAllText(Path.Combine(folder,"settings.ini"),"sun="+(sun.Checked?1:0)+"\r\nmoney="+(money.Checked?1:0)+"\r\ndelay="+delay.Value+"\r\ngardenCabbage="+(cabbage.Checked?1:0)+"\r\nautoRake="+(autoRake.Checked?1:0)+"\r\nautoFeedTree="+(autoFeed.Checked?1:0)+"\r\nautoFertilizer="+(autoFertilizer.Checked?1:0)+"\r\nautoBugSpray="+(autoBugSpray.Checked?1:0)+"\r\nautoTreeFood="+(autoTreeFood.Checked?1:0)+"\r\n");}catch{}
    }
    static Process FindGame() {
        foreach(var p in Process.GetProcessesByName("Replanted")) {
            try{if(string.Equals(p.MainModule.FileName,GamePath,StringComparison.OrdinalIgnoreCase))return p;}catch{}p.Dispose();
        }return null;
    }
    async void StartOrAttach() {
        if(busy)return;busy=true;launch.Enabled=false;
        try {
            if(!File.Exists(GamePath))throw new Exception("找不到游戏："+GamePath);
            string hash;using(var sha=SHA256.Create())using(var input=File.OpenRead(Path.Combine(Path.GetDirectoryName(GamePath),"GameAssembly.dll")))hash=BitConverter.ToString(sha.ComputeHash(input)).Replace("-","");
            if(hash!=AssemblyHash)throw new Exception("游戏版本已变化，请重新适配后再使用。");
            game=FindGame();
            if(game==null)game=Process.Start(new ProcessStartInfo(GamePath){WorkingDirectory=Path.GetDirectoryName(GamePath)});
            SetStatus("等待游戏加载……");
            var watch=Stopwatch.StartNew();bool ready=false;
            while(watch.ElapsedMilliseconds<90000) {
                if(game==null||game.HasExited)throw new Exception("游戏已退出。");
                game.Refresh();bool assembly=false;
                foreach(ProcessModule m in game.Modules) {
                    string n=m.ModuleName.ToLowerInvariant();
                    if(n=="roh_pvzassist.dll"||n=="pvzoverlay.dll")throw new Exception("检测到旧扩展，请正常退出游戏后只使用新版启动。");
                    if(n=="gameassembly.dll")assembly=true;
                }
                if(assembly&&game.MainWindowHandle!=IntPtr.Zero&&watch.ElapsedMilliseconds>=10000){ready=true;break;}
                await Task.Delay(400);
                if(closing)return;
            }
            if(!ready)throw new Exception("游戏加载超时，请进入主菜单后重试。");
            ReleaseMap();map=MemoryMappedFile.CreateOrOpen("Local\\PVZQoL2_"+game.Id,4096,MemoryMappedFileAccess.ReadWrite);
            view=map.CreateViewAccessor();bool loaded=view.ReadUInt32(0)==0x32515A50&&view.ReadInt32(12)==1;
            if(loaded&&view.ReadUInt32(4)!=5)throw new Exception("游戏仍加载旧版扩展，请完全退出游戏和助手后重新打开。");
            if(!loaded){view.Write(0,0x32515A50u);view.Write(4,5u);view.Write(12,0);}
            ApplySettings();
            if(!loaded) {
                SetStatus("正在启用自动拾取与花园外观……");
                await Task.Run(delegate{Native.LoadInto(game,Path.Combine(folder,"PVZQoL2.dll"));});
            }
            connected=true;loadTime=Native.GetTickCount64();SetStatus("扩展初始化中……");
        } catch(Exception ex) {SetStatus(ex.Message);WriteDiagnostic(ex.Message);ReleaseMap();MessageBox.Show(this,ex.Message,"无法启用",MessageBoxButtons.OK,MessageBoxIcon.Warning);}
        finally {busy=false;if(!closing)launch.Enabled=true;}
    }
    void SetStatus(string text){lastState=text;if(!closing)status.Text=text;}
    void ReleaseMap(){connected=false;if(view!=null){view.Dispose();view=null;}if(map!=null){map.Dispose();map=null;}}
    int Read(int offset){return view==null?0:view.ReadInt32(offset);}
    void Tick() {
        if(closing||!connected||game==null||view==null)return;
        try {
            if(game.HasExited){WriteDiagnostic("游戏已正常结束");SetStatus("游戏已退出，所有功能已停止");ReleaseMap();return;}
            int nativeStatus=Read(12);
            if(nativeStatus==3)SetStatus("初始化失败，请查看 native.log");
            else if(nativeStatus==2)SetStatus("游戏正在退出，功能已停止");
            else if(nativeStatus==1) {
                SetStatus("已启用 · 拾取保留游戏原生动画");
                stats.Text="阳光："+Read(32)+" 次启动 / "+Read(40)+" 次入账　金币："+Read(36)+" / "+Read(44)
                    +"\r\n收集动画帧："+Read(52)+"　异常："+Read(48)
                    +"\r\n花园彩蛋模型："+Read(64)+" 次（切换外观后重新进入花园）"
                    +"\r\n干草叉补购："+Read(68)+" 次，花费 "+(Read(72)*10L)+" 金币"
                    +"\r\n补购：肥料 "+Read(80)+" 包 / 杀虫剂 "+Read(84)+" 包 / 树肥 "+Read(88)+" 包"
                    +"\r\n耗材花费："+(Read(92)*10L)+" 金币；余额不足跳过："+Read(96)
                    +"\r\n自动喂树："+Read(100)+" 次；树高："+Read(104)+" 英尺（进入树界面后更新）";
            } else if(Native.GetTickCount64()-loadTime>15000)SetStatus("初始化超时，请退出游戏并查看 native.log");
            if(Native.GetTickCount64()-lastDiagnostic>2000){lastDiagnostic=Native.GetTickCount64();WriteDiagnostic(lastState);}
        }catch(Exception ex){SetStatus(ex.Message);}
    }
    static string Json(string value){return "\""+value.Replace("\\","\\\\").Replace("\"","\\\"").Replace("\r"," ").Replace("\n"," ")+"\"";}
    void WriteDiagnostic(string message) {
        try {
            string line="{\n  \"time\": "+Json(DateTime.Now.ToString("o"))
                +",\n  \"state\": "+Json(message)+",\n  \"pid\": "+(game==null?0:game.Id)
                +",\n  \"nativeStatus\": "+Read(12)+",\n  \"starts\": "+Read(24)+",\n  \"completes\": "+Read(28)
                +",\n  \"sunStarts\": "+Read(32)+",\n  \"moneyStarts\": "+Read(36)
                +",\n  \"sunCompletes\": "+Read(40)+",\n  \"moneyCompletes\": "+Read(44)
                +",\n  \"errors\": "+Read(48)+",\n  \"animationFrames\": "+Read(52)
                +",\n  \"gardenCabbageRequests\": "+Read(60)+",\n  \"gardenCabbageOverrides\": "+Read(64)
                +",\n  \"gardenCabbageEnabled\": "+(cabbage.Checked?"true":"false")
                +",\n  \"rakePurchases\": "+Read(68)+",\n  \"rakeSpentDisplayedCoins\": "+(Read(72)*10L)
                +",\n  \"rakeInsufficient\": "+Read(76)+",\n  \"autoRakeEnabled\": "+(autoRake.Checked?"true":"false")
                +",\n  \"fertilizerPurchases\": "+Read(80)+",\n  \"bugSprayPurchases\": "+Read(84)+",\n  \"treeFoodPurchases\": "+Read(88)
                +",\n  \"supplySpentDisplayedCoins\": "+(Read(92)*10L)+",\n  \"supplyInsufficient\": "+Read(96)
                +",\n  \"treeFeeds\": "+Read(100)+",\n  \"treeHeight\": "+Read(104)
                +",\n  \"autoFeedTreeEnabled\": "+(autoFeed.Checked?"true":"false")
                +",\n  \"autoFertilizerEnabled\": "+(autoFertilizer.Checked?"true":"false")
                +",\n  \"autoBugSprayEnabled\": "+(autoBugSpray.Checked?"true":"false")
                +",\n  \"autoTreeFoodEnabled\": "+(autoTreeFood.Checked?"true":"false")+"\n}\n";
            File.WriteAllText(Path.Combine(folder,"diagnostics.json"),line);
        }catch{}
    }
}
static class Program {
    [STAThread] static void Main(string[] args) {
        bool created;using(var mutex=new System.Threading.Mutex(true,"Local\\PVZQoL2_Helper",out created)) {
            if(!created){MessageBox.Show("新版助手已经在运行，请查看任务栏。");return;}
            try{Native.SetProcessDpiAwarenessContext((IntPtr)(-4));}catch{Native.SetProcessDPIAware();}
            Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm(Array.IndexOf(args,"--auto")>=0));
        }
    }
}
