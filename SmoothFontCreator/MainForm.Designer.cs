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
      MsgLabel = new Label();
      HexLabel = new Label();
      ASCIILabel = new Label();
      TestCharTextBox = new TextBox();
      TestCharsTextBox = new TextBox();
      FontComboBox = new ComboBox();
      MetricsPanel = new Panel();
      CharPanel = new Panel();
      AllCharsPanel = new Panel();
      tabPage2 = new TabPage();
      button1 = new Button();
      tabControl1.SuspendLayout();
      tabPage1.SuspendLayout();
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
      tabControl1.Size = new Size(1514, 981);
      tabControl1.TabIndex = 0;
      // 
      // tabPage1
      // 
      tabPage1.Controls.Add(button1);
      tabPage1.Controls.Add(MsgLabel);
      tabPage1.Controls.Add(HexLabel);
      tabPage1.Controls.Add(ASCIILabel);
      tabPage1.Controls.Add(TestCharTextBox);
      tabPage1.Controls.Add(TestCharsTextBox);
      tabPage1.Controls.Add(FontComboBox);
      tabPage1.Controls.Add(MetricsPanel);
      tabPage1.Controls.Add(CharPanel);
      tabPage1.Controls.Add(AllCharsPanel);
      tabPage1.Location = new Point(4, 34);
      tabPage1.Name = "tabPage1";
      tabPage1.Padding = new Padding(3);
      tabPage1.Size = new Size(1506, 943);
      tabPage1.TabIndex = 0;
      tabPage1.Text = "tabPage1";
      tabPage1.UseVisualStyleBackColor = true;
      // 
      // MsgLabel
      // 
      MsgLabel.AutoSize = true;
      MsgLabel.Location = new Point(1303, 749);
      MsgLabel.Name = "MsgLabel";
      MsgLabel.Size = new Size(47, 25);
      MsgLabel.TabIndex = 8;
      MsgLabel.Text = "msg";
      // 
      // HexLabel
      // 
      HexLabel.AutoSize = true;
      HexLabel.Location = new Point(1350, 457);
      HexLabel.Name = "HexLabel";
      HexLabel.Size = new Size(59, 25);
      HexLabel.TabIndex = 7;
      HexLabel.Text = "label1";
      // 
      // ASCIILabel
      // 
      ASCIILabel.AutoSize = true;
      ASCIILabel.Location = new Point(1350, 408);
      ASCIILabel.Name = "ASCIILabel";
      ASCIILabel.Size = new Size(59, 25);
      ASCIILabel.TabIndex = 6;
      ASCIILabel.Text = "label1";
      // 
      // TestCharTextBox
      // 
      TestCharTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      TestCharTextBox.Location = new Point(1350, 360);
      TestCharTextBox.Name = "TestCharTextBox";
      TestCharTextBox.Size = new Size(150, 31);
      TestCharTextBox.TabIndex = 5;
      TestCharTextBox.Text = "0";
      // 
      // TestCharsTextBox
      // 
      TestCharsTextBox.Anchor = AnchorStyles.Top | AnchorStyles.Right;
      TestCharsTextBox.Location = new Point(1225, 45);
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
      FontComboBox.Location = new Point(1225, 6);
      FontComboBox.Name = "FontComboBox";
      FontComboBox.Size = new Size(275, 33);
      FontComboBox.TabIndex = 3;
      // 
      // MetricsPanel
      // 
      MetricsPanel.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
      MetricsPanel.BackColor = Color.Black;
      MetricsPanel.Location = new Point(616, 223);
      MetricsPanel.Name = "MetricsPanel";
      MetricsPanel.Size = new Size(603, 714);
      MetricsPanel.TabIndex = 2;
      // 
      // CharPanel
      // 
      CharPanel.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left;
      CharPanel.BackColor = Color.DimGray;
      CharPanel.Location = new Point(6, 223);
      CharPanel.Name = "CharPanel";
      CharPanel.Size = new Size(604, 714);
      CharPanel.TabIndex = 1;
      // 
      // AllCharsPanel
      // 
      AllCharsPanel.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
      AllCharsPanel.BackColor = Color.Black;
      AllCharsPanel.Location = new Point(6, 6);
      AllCharsPanel.Name = "AllCharsPanel";
      AllCharsPanel.Size = new Size(1213, 211);
      AllCharsPanel.TabIndex = 0;
      // 
      // tabPage2
      // 
      tabPage2.Location = new Point(4, 34);
      tabPage2.Name = "tabPage2";
      tabPage2.Padding = new Padding(3);
      tabPage2.Size = new Size(1506, 943);
      tabPage2.TabIndex = 1;
      tabPage2.Text = "tabPage2";
      tabPage2.UseVisualStyleBackColor = true;
      // 
      // button1
      // 
      button1.Location = new Point(1283, 607);
      button1.Name = "button1";
      button1.Size = new Size(112, 34);
      button1.TabIndex = 9;
      button1.Text = "button1";
      button1.UseVisualStyleBackColor = true;
      button1.Click += button1_Click;
      // 
      // MainForm
      // 
      AutoScaleDimensions = new SizeF(10F, 25F);
      AutoScaleMode = AutoScaleMode.Font;
      ClientSize = new Size(1538, 1005);
      Controls.Add(tabControl1);
      Name = "MainForm";
      Text = "Smooth Font Creator for TFT_eSPI";
      tabControl1.ResumeLayout(false);
      tabPage1.ResumeLayout(false);
      tabPage1.PerformLayout();
      ResumeLayout(false);
   }

   #endregion

   private TabControl tabControl1;
   private TabPage tabPage1;
   private TabPage tabPage2;
   private Panel AllCharsPanel;
   private Panel CharPanel;
   private Panel MetricsPanel;
   private ComboBox FontComboBox;
   private TextBox TestCharsTextBox;
   private TextBox TestCharTextBox;
   private Label HexLabel;
   private Label ASCIILabel;
   private Label MsgLabel;
   private Button button1;
}
