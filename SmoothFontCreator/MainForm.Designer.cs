namespace SmoothFontCreator;

partial class MainForm
{
   /// <summary>
   ///  Required designer variable.
   /// </summary>
   private System.ComponentModel.IContainer components = null;

   /// <summary>
   ///  Clean up any resources being used.
   /// </summary>
   /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
   protected override void Dispose(bool disposing)
   {
      if (disposing && (components != null))
      {
         components.Dispose();
      }
      base.Dispose(disposing);
   }

   #region Windows Form Designer generated code

   /// <summary>
   ///  Required method for Designer support - do not modify
   ///  the contents of this method with the code editor.
   /// </summary>
   private void InitializeComponent()
   {
      tabControl1 = new TabControl();
      tabPage1 = new TabPage();
      HexLabel = new Label();
      button2 = new Button();
      label8 = new Label();
      MonospaceCheckBox = new CheckBox();
      CharPreviewPanel = new Panel();
      DesiredWidthTextBox = new TextBox();
      ComputedWidthTextBox = new TextBox();
      DesiredHeightTextBox = new TextBox();
      label1 = new Label();
      button1 = new Button();
      MsgLabel = new Label();
      label4 = new Label();
      label3 = new Label();
      label2 = new Label();
      label6 = new Label();
      label5 = new Label();
      label7 = new Label();
      TestCharTextBox = new TextBox();
      TestCharsTextBox = new TextBox();
      FontComboBox = new ComboBox();
      CharPanel = new Panel();
      AllCharsPanel = new Panel();
      tabPage2 = new TabPage();
      panel1 = new Panel();
      tabControl1.SuspendLayout();
      tabPage1.SuspendLayout();
      tabPage2.SuspendLayout();
      SuspendLayout();
      // 
      // tabControl1
      // 
      tabControl1.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      tabControl1.Controls.Add(tabPage1);
      tabControl1.Controls.Add(tabPage2);
      tabControl1.Location = new Point(12, 12);
      tabControl1.Name = "tabControl1";
      tabControl1.SelectedIndex = 0;
      tabControl1.Size = new Size(1826, 977);
      tabControl1.TabIndex = 0;
      // 
      // tabPage1
      // 
      tabPage1.Controls.Add(HexLabel);
      tabPage1.Controls.Add(button2);
      tabPage1.Controls.Add(label8);
      tabPage1.Controls.Add(MonospaceCheckBox);
      tabPage1.Controls.Add(CharPreviewPanel);
      tabPage1.Controls.Add(DesiredWidthTextBox);
      tabPage1.Controls.Add(ComputedWidthTextBox);
      tabPage1.Controls.Add(DesiredHeightTextBox);
      tabPage1.Controls.Add(label1);
      tabPage1.Controls.Add(button1);
      tabPage1.Controls.Add(MsgLabel);
      tabPage1.Controls.Add(label4);
      tabPage1.Controls.Add(label3);
      tabPage1.Controls.Add(label2);
      tabPage1.Controls.Add(label6);
      tabPage1.Controls.Add(label5);
      tabPage1.Controls.Add(label7);
      tabPage1.Controls.Add(TestCharTextBox);
      tabPage1.Controls.Add(TestCharsTextBox);
      tabPage1.Controls.Add(FontComboBox);
      tabPage1.Controls.Add(CharPanel);
      tabPage1.Controls.Add(AllCharsPanel);
      tabPage1.Location = new Point(4, 34);
      tabPage1.Name = "tabPage1";
      tabPage1.Padding = new Padding(3);
      tabPage1.Size = new Size(1818, 939);
      tabPage1.TabIndex = 0;
      tabPage1.Text = "tabPage1";
      tabPage1.UseVisualStyleBackColor = true;
      // 
      // HexLabel
      // 
      HexLabel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      HexLabel.AutoSize = true;
      HexLabel.Location = new Point(1760, 363);
      HexLabel.Name = "HexLabel";
      HexLabel.Size = new Size(50, 25);
      HexLabel.TabIndex = 18;
      HexLabel.Text = "0x00";
      // 
      // button2
      // 
      button2.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      button2.Location = new Point(1595, 647);
      button2.Name = "button2";
      button2.Size = new Size(112, 34);
      button2.TabIndex = 17;
      button2.Text = "Find Maxes";
      button2.UseVisualStyleBackColor = true;
      button2.Click += button2_Click;
      // 
      // label8
      // 
      label8.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label8.AutoSize = true;
      label8.Location = new Point(1553, 482);
      label8.Name = "label8";
      label8.Size = new Size(109, 25);
      label8.TabIndex = 16;
      label8.Text = "Monospace:";
      // 
      // MonospaceCheckBox
      // 
      MonospaceCheckBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      MonospaceCheckBox.AutoSize = true;
      MonospaceCheckBox.Location = new Point(1677, 486);
      MonospaceCheckBox.Name = "MonospaceCheckBox";
      MonospaceCheckBox.Size = new Size(22, 21);
      MonospaceCheckBox.TabIndex = 15;
      MonospaceCheckBox.UseVisualStyleBackColor = true;
      // 
      // CharPreviewPanel
      // 
      CharPreviewPanel.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
      CharPreviewPanel.BackColor = Color.DimGray;
      CharPreviewPanel.Location = new Point(616, 223);
      CharPreviewPanel.Name = "CharPreviewPanel";
      CharPreviewPanel.Size = new Size(916, 711);
      CharPreviewPanel.TabIndex = 2;
      // 
      // DesiredWidthTextBox
      // 
      DesiredWidthTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      DesiredWidthTextBox.Location = new Point(1677, 558);
      DesiredWidthTextBox.Name = "DesiredWidthTextBox";
      DesiredWidthTextBox.Size = new Size(94, 31);
      DesiredWidthTextBox.TabIndex = 14;
      DesiredWidthTextBox.Text = "24";
      // 
      // ComputedWidthTextBox
      // 
      ComputedWidthTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      ComputedWidthTextBox.Location = new Point(1677, 519);
      ComputedWidthTextBox.Name = "ComputedWidthTextBox";
      ComputedWidthTextBox.ReadOnly = true;
      ComputedWidthTextBox.Size = new Size(94, 31);
      ComputedWidthTextBox.TabIndex = 13;
      ComputedWidthTextBox.Text = "32";
      // 
      // DesiredHeightTextBox
      // 
      DesiredHeightTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      DesiredHeightTextBox.Location = new Point(1677, 437);
      DesiredHeightTextBox.Name = "DesiredHeightTextBox";
      DesiredHeightTextBox.Size = new Size(94, 31);
      DesiredHeightTextBox.TabIndex = 12;
      DesiredHeightTextBox.Text = "16";
      DesiredHeightTextBox.TextChanged += DesiredHeightTextBox_TextChanged;
      // 
      // label1
      // 
      label1.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label1.AutoSize = true;
      label1.Location = new Point(1607, 522);
      label1.Name = "label1";
      label1.Size = new Size(64, 25);
      label1.TabIndex = 10;
      label1.Text = "Width:";
      // 
      // button1
      // 
      button1.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      button1.Location = new Point(1595, 607);
      button1.Name = "button1";
      button1.Size = new Size(112, 34);
      button1.TabIndex = 9;
      button1.Text = "button1";
      button1.UseVisualStyleBackColor = true;
      button1.Click += button1_Click;
      // 
      // MsgLabel
      // 
      MsgLabel.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      MsgLabel.AutoSize = true;
      MsgLabel.Location = new Point(1615, 749);
      MsgLabel.Name = "MsgLabel";
      MsgLabel.Size = new Size(47, 25);
      MsgLabel.TabIndex = 8;
      MsgLabel.Text = "msg";
      // 
      // label4
      // 
      label4.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label4.AutoSize = true;
      label4.Location = new Point(1777, 443);
      label4.Name = "label4";
      label4.Size = new Size(31, 25);
      label4.TabIndex = 7;
      label4.Text = "px";
      // 
      // label3
      // 
      label3.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label3.AutoSize = true;
      label3.Location = new Point(1777, 522);
      label3.Name = "label3";
      label3.Size = new Size(31, 25);
      label3.TabIndex = 7;
      label3.Text = "px";
      // 
      // label2
      // 
      label2.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label2.AutoSize = true;
      label2.Location = new Point(1777, 561);
      label2.Name = "label2";
      label2.Size = new Size(31, 25);
      label2.TabIndex = 7;
      label2.Text = "px";
      // 
      // label6
      // 
      label6.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label6.AutoSize = true;
      label6.Location = new Point(1542, 561);
      label6.Name = "label6";
      label6.Size = new Size(129, 25);
      label6.TabIndex = 7;
      label6.Text = "Desired Width:";
      // 
      // label5
      // 
      label5.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label5.AutoSize = true;
      label5.Location = new Point(1610, 363);
      label5.Name = "label5";
      label5.Size = new Size(52, 25);
      label5.TabIndex = 6;
      label5.Text = "Char:";
      // 
      // label7
      // 
      label7.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      label7.AutoSize = true;
      label7.Location = new Point(1537, 437);
      label7.Name = "label7";
      label7.Size = new Size(134, 25);
      label7.TabIndex = 6;
      label7.Text = "Desired Height:";
      // 
      // TestCharTextBox
      // 
      TestCharTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      TestCharTextBox.Location = new Point(1668, 360);
      TestCharTextBox.Name = "TestCharTextBox";
      TestCharTextBox.Size = new Size(81, 31);
      TestCharTextBox.TabIndex = 5;
      TestCharTextBox.Text = "$";
      TestCharTextBox.TextChanged += TestCharTextBox_TextChanged;
      // 
      // TestCharsTextBox
      // 
      TestCharsTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      TestCharsTextBox.Location = new Point(1537, 45);
      TestCharsTextBox.Multiline = true;
      TestCharsTextBox.Name = "TestCharsTextBox";
      TestCharsTextBox.Size = new Size(275, 309);
      TestCharsTextBox.TabIndex = 4;
      TestCharsTextBox.Text = "0@123.xyz\r\n0@123.xyz\r\n0@123.xyz\r\n0@123.xyz\r\n0@123.xyz\r\n0@123.xyz\r\n0@123.xyz";
      // 
      // FontComboBox
      // 
      FontComboBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      FontComboBox.FormattingEnabled = true;
      FontComboBox.Location = new Point(1537, 6);
      FontComboBox.Name = "FontComboBox";
      FontComboBox.Size = new Size(275, 33);
      FontComboBox.TabIndex = 3;
      // 
      // CharPanel
      // 
      CharPanel.BackColor = Color.DimGray;
      CharPanel.Location = new Point(6, 223);
      CharPanel.Name = "CharPanel";
      CharPanel.Size = new Size(604, 711);
      CharPanel.TabIndex = 1;
      // 
      // AllCharsPanel
      // 
      AllCharsPanel.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
      AllCharsPanel.BackColor = Color.Black;
      AllCharsPanel.Location = new Point(6, 6);
      AllCharsPanel.Name = "AllCharsPanel";
      AllCharsPanel.Size = new Size(1525, 211);
      AllCharsPanel.TabIndex = 0;
      // 
      // tabPage2
      // 
      tabPage2.Controls.Add(panel1);
      tabPage2.Location = new Point(4, 34);
      tabPage2.Name = "tabPage2";
      tabPage2.Padding = new Padding(3);
      tabPage2.Size = new Size(1818, 1539);
      tabPage2.TabIndex = 1;
      tabPage2.Text = "tabPage2";
      tabPage2.UseVisualStyleBackColor = true;
      // 
      // panel1
      // 
      panel1.AutoScroll = true;
      panel1.BackColor = Color.DimGray;
      panel1.Location = new Point(6, 6);
      panel1.Name = "panel1";
      panel1.Size = new Size(2048, 2048);
      panel1.TabIndex = 0;
      panel1.Paint += panel1_Paint;
      // 
      // MainForm
      // 
      AutoScaleDimensions = new SizeF(10F, 25F);
      AutoScaleMode = AutoScaleMode.Font;
      ClientSize = new Size(1850, 1001);
      Controls.Add(tabControl1);
      Name = "MainForm";
      Text = "Smooth Font Creator for TFT_eSPI";
      tabControl1.ResumeLayout(false);
      tabPage1.ResumeLayout(false);
      tabPage1.PerformLayout();
      tabPage2.ResumeLayout(false);
      ResumeLayout(false);
   }

   #endregion

   private TabControl tabControl1;
   private TabPage tabPage1;
   private TabPage tabPage2;
   private Panel AllCharsPanel;
   private Panel CharPanel;
   private ComboBox FontComboBox;
   private TextBox TestCharsTextBox;
   private TextBox TestCharTextBox;
   private Label label6;
   private Label label7;
   private Label MsgLabel;
   private Button button1;
   private Label label1;
   private TextBox DesiredWidthTextBox;
   private TextBox ComputedWidthTextBox;
   private TextBox DesiredHeightTextBox;
   private Label label4;
   private Label label3;
   private Label label2;
   private Label label5;
   private Panel CharPreviewPanel;
   private Label label8;
   private CheckBox MonospaceCheckBox;
   private Button button2;
   private Label HexLabel;
   private Panel panel1;
}
