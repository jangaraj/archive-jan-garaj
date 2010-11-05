package fun;

import java.net.URL;
import com.gargoylesoftware.htmlunit.*;
import com.gargoylesoftware.htmlunit.html.*;

/**
 * @author Jan Garaj
 * This is full automatic selfchatting application.
 * All selfchattings converge to sentences about programing in Visual Basic.
 * Warning: usage of site www.ludvik.sk in this code is unauthorized,
 * use it only for fun
 */
public class LudvikOnline {

    public static void main(String[] args) throws Exception {
        final LudvikOnline b = new LudvikOnline();
        b.run();
    }

    public void run() throws Exception {
        final WebClient browser = new WebClient(BrowserVersion.FIREFOX_3);
        Integer i = 1;
        String textLudvik1 = "";
        String textLudvik2 = "Ahoj";
        URL urlLudvik1 = null;
        URL urlLudvik2 = null;
        HtmlPage pageLudvik1 = null;
        HtmlPage pageLudvik2 = null;
        HtmlElement tempLudvik1 = null;
        HtmlElement tempLudvik2 = null;
        System.out.println("0\nMato: " + textLudvik2);
        while (true) {
            urlLudvik1 = new URL("http://www.ludvik.sk/system.php?sentence=" + textLudvik2 + "&name=Mato&time=" + (System.currentTimeMillis() + i));
            pageLudvik1 = (HtmlPage) browser.getPage(urlLudvik1);
            tempLudvik1 = (HtmlElement) pageLudvik1.getByXPath("//div[@class='ludvik_server']").get(0);
            textLudvik1 = tempLudvik1.asText();
            System.out.println(i+"\nJano: " + textLudvik1);
            urlLudvik2 = new URL("http://www.ludvik.sk/system.php?sentence=" + textLudvik1 + "&name=Jano&time=" + (System.currentTimeMillis() + i));
            pageLudvik2 = (HtmlPage) browser.getPage(urlLudvik2);
            tempLudvik2 = (HtmlElement) pageLudvik2.getByXPath("//div[@class='ludvik_server']").get(0);
            textLudvik2 = tempLudvik2.asText();
            System.out.println("Mato: " + textLudvik2);
            i++;
            Thread.sleep(1500);
        }
    }
}
